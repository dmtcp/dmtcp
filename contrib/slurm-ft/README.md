# SLURM-FT plugin

This plugin can be used for simple fault-tolerance with SLURM. When
submitting jobs use this plugin along with the `--signal` SBATCH
option. The `--signal` option is used to configure SLURM to send a
specified signal to the job at a specified time before the job ends.

The plugin works by installing a signal handler on initialization. This
signal handler uses the `dmtcp_checkpoint()` API to request for a
checkpoint when triggered.

## Usage

   $ bin/dmtcp_launch --with-plugin /path/to/libdmtcp_slurm-ft.so a.out

## Example sbatch script

       #SBATCH --ntasks=1         # One process
       #SBATCH --signal=36@10     # Ask SLURM to send a signal within 10 seconds of job end time
       srun dmtcp_launch --with-plugin /path/to/libdmtcp_slurm-ft.so /path/to/application

## Limitations

The plugin relies on the `dmtcp_checkpoint()` API for requesting
checkpoints.

When running _N_ parallel processes, the plugin library in each process
will see the signal, and consequently, the coordinator will see requests
for _N_ checkpoints. A possible solution is to use the coordinator's
pub-sub service to ensure that only one checkpoint is requested.
