/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "jfilesystem.h"
#include <sys/types.h>
#include <unistd.h>
#include "jconvert.h"
#include <dirent.h>
#include "errno.h"
#include <sys/utsname.h>

namespace 
{
    std::string _GetProgramExe()
    {
        std::string exe = "/proc/" + jalib::XToString(getpid()) + "/exe";
        std::string exeRes = jalib::Filesystem::ResolveSymlink(exe);
        JASSERT(exe != exeRes)(exe).Text("problem with /proc/self/exe");
        return exeRes;
    }
    
    std::string _FileBaseName(const std::string& str)
    {
        int lastSlash = 0;
        for(size_t i = 0; i<str.length(); ++i)
            if(str[i] == '/')
                lastSlash = i;
        return str.substr(lastSlash+1);
    }
    
    std::string _DirBaseName(const std::string& str)
    {
        int lastSlash = 0;
        for(size_t i = 0; i<str.length(); ++i)
            if(str[i] == '/')
                lastSlash = i;
        return str.substr(0,lastSlash);
    }
}

std::string jalib::Filesystem::GetProgramDir()
{
    static std::string value = _DirBaseName(GetProgramPath());
    return value;
}

std::string jalib::Filesystem::GetProgramName()
{
    static std::string value = _FileBaseName(GetProgramPath());
    return value;
}

std::string jalib::Filesystem::GetProgramPath()
{
    static std::string value = _GetProgramExe();
    return value;
}


std::string jalib::Filesystem::ResolveSymlink(const std::string& path)
{
    char buf [1024];
    memset(buf,0,sizeof(buf));
    int len = readlink(path.c_str(), buf, sizeof(buf)-1);
    if(len <= 0) 
        return "";
    return buf;
}

bool jalib::Filesystem::FileExists(const std::string& str)
{
    FILE* fp = fopen(str.c_str(),"r");
    if(fp != NULL) fclose(fp);
    return fp != NULL;
}

#define FHU_TRY_DIR(expr) {\
    std::string pth = expr; \
    if(FileExists(pth)) \
        return pth;}


std::string jalib::Filesystem::FindHelperUtility(const std::string& file, bool dieOnError /*= true*/)
{
    const char* d = NULL;
    if((d=getenv("JALIB_UTILITY_DIR")) != NULL)
    {
        std::string udir = d;
        FHU_TRY_DIR(udir + "/" + file);
        FHU_TRY_DIR(udir + "/../" + file);
        FHU_TRY_DIR(udir + "/../../" + file);
        FHU_TRY_DIR(udir + "/../../../" + file);
        FHU_TRY_DIR(udir + "/mtcp/" + file);
        FHU_TRY_DIR(udir + "/../mtcp/" + file);
        FHU_TRY_DIR(udir + "/../../mtcp/" + file);
        FHU_TRY_DIR(udir + "/../../../mtcp/" + file);
    }
    FHU_TRY_DIR(GetProgramDir() + "/" + file);
    FHU_TRY_DIR(GetProgramDir() + "/../" + file);
    FHU_TRY_DIR(GetProgramDir() + "/../../" + file);
    FHU_TRY_DIR(GetProgramDir() + "/../../../" + file);
    FHU_TRY_DIR("./" + file);
    FHU_TRY_DIR("../" + file);
    FHU_TRY_DIR("../../" + file);
    FHU_TRY_DIR("../../../" + file);
    FHU_TRY_DIR("/usr/bin/" + file);
    FHU_TRY_DIR("/bin/" + file);
    JASSERT(!dieOnError)(file)(GetProgramDir())(d)
            .Text("failed to find needed file");
    return file;
}


std::vector<std::string> jalib::Filesystem::GetProgramArgs()
{
    std::vector<std::string> rv;
    
    std::string path = "/proc/" + jalib::XToString(getpid()) + "/cmdline";
    FILE* args = fopen(path.c_str(),"r");
    
    JASSERT(args != NULL)(path).Text("failed to open command line");
    
    char * lineptr = (char*)malloc(512); //getdelim will auto-grow this buffer
    size_t len = 511;
            
    while(getdelim(&lineptr, &len, '\0', args) >= 0)
    {
        rv.push_back(lineptr);
    }
    
    free(lineptr);
    
    return rv;
}

std::vector<int> jalib::Filesystem::ListOpenFds()
{
    std::string dir = "/proc/" + XToString(getpid()) + "/fd";
    std::vector<int> rv;
    struct dirent **namelist;
    char* p;
    int nents = scandir(dir.c_str(), &namelist, NULL, versionsort);
    JASSERT(nents >= 0)(dir)(JASSERT_ERRNO).Text("failed to open directory");

    for (int i = 0; i < nents; i ++) {
    struct dirent * de = namelist[i];
    int fdnum = strtol (de -> d_name, &p, 10);
    if( *p == 0 && fdnum >= 0 ) {
        rv.push_back(fdnum);
    }
    free (de);
  }
  free (namelist);

  return rv;
}


std::string jalib::Filesystem::GetCurrentHostname()
{
  struct utsname tmp;
  memset(&tmp,0,sizeof(tmp));
  uname(&tmp);
  std::string name = "unknown";
  if(tmp.nodename != 0)
    name = tmp.nodename;
//   #ifdef _GNU_SOURCE
//   if(tmp.domainname != 0)
//     name += std::string(".") + tmp.domainname;
//   #endif
  return name;
}
