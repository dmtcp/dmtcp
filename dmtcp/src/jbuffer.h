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
#ifndef JALIBJBUFFER_H
#define JALIBJBUFFER_H

namespace jalib {

/**
	@author Jason Ansel <jansel@ccs.neu.edu>
*/
class JBuffer{
public:
    JBuffer(int size = 0);
    JBuffer(const char* source, int size);
    JBuffer(const JBuffer& that);
    ~JBuffer();
    jalib::JBuffer& operator=(const JBuffer& that);
    
    
    const char* buffer() const;
    char* buffer();
    int size() const;
    operator char* () {return buffer();}
    operator const char* () {return buffer();}
	
private:
    char * _buffer;
    int    _size;
};

}

#endif
