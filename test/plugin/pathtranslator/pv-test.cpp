#include <sys/param.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <string>

void write_line(std::string path, int count)
{
    std::ofstream f;
    f.open(path.c_str(), std::ios_base::app);
    f << count << " appending" << std::endl;
    f.flush();
    f.close();
}

int main(int argc, const char *argv[])
{
    std::string fil = "pv-test.txt";
    char _cwd[MAXPATHLEN];
    std::string cwd;
    char last;
    int count = 0;

    getcwd(_cwd, sizeof(_cwd));
    cwd = _cwd;
    last = cwd.at(cwd.size() - 1);

    std::string binpath = cwd + "/bin" + last + "/" +  fil;
    std::string docpath = cwd + "/doc" + last + "/" +  fil;
    std::string libpath = cwd + "/lib" + last + "/" +  fil;

    while (1) {
        std::cout << "[" << count << "] Appending..." << std::endl;
        write_line(binpath, count);
        write_line(docpath, count);
        write_line(libpath, count);
        count++;
        sleep(1);
    }

    return 0;
}
