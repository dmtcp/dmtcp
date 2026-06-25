try:
    from dataclasses import dataclass, field, replace
except ImportError:
    _MISSING = object()

    class _Field:
        def __init__(self, default_factory):
            self.default_factory = default_factory

    def field(default_factory):
        return _Field(default_factory)

    def dataclass(cls=None, *, frozen=False):
        def decorate(record_class):
            names = tuple(record_class.__annotations__)
            defaults = {}
            factories = {}
            for name in names:
                value = getattr(record_class, name, _MISSING)
                if isinstance(value, _Field):
                    factories[name] = value.default_factory
                    delattr(record_class, name)
                elif value is not _MISSING:
                    defaults[name] = value

            record_class.__autotest_dataclass_fields__ = names

            def __init__(self, *args, **kwargs):
                if len(args) > len(names):
                    raise TypeError("too many positional arguments")
                values = dict(zip(names, args))
                for name in names[len(args):]:
                    if name in kwargs:
                        values[name] = kwargs.pop(name)
                    elif name in factories:
                        values[name] = factories[name]()
                    elif name in defaults:
                        values[name] = defaults[name]
                    else:
                        raise TypeError(f"missing argument: {name}")
                if kwargs:
                    name = next(iter(kwargs))
                    raise TypeError(f"unexpected argument: {name}")
                for name in names:
                    object.__setattr__(self, name, values[name])

            def __eq__(self, other):
                if other.__class__ is not record_class:
                    return NotImplemented
                return all(
                    getattr(self, name) == getattr(other, name)
                    for name in names
                )

            def __repr__(self):
                args = ", ".join(
                    f"{name}={getattr(self, name)!r}" for name in names
                )
                return f"{record_class.__name__}({args})"

            record_class.__init__ = __init__
            record_class.__eq__ = __eq__
            record_class.__repr__ = __repr__
            if frozen:
                def __setattr__(self, name, value):
                    if name in names and hasattr(self, name):
                        raise AttributeError("can't set attribute")
                    object.__setattr__(self, name, value)
                record_class.__setattr__ = __setattr__
            return record_class

        if cls is None:
            return decorate
        return decorate(cls)

    def replace(instance, **changes):
        values = {
            name: getattr(instance, name)
            for name in instance.__autotest_dataclass_fields__
        }
        values.update(changes)
        return instance.__class__(**values)
