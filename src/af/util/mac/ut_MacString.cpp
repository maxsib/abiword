/* AbiSource Program Utilities
 * Copyright (C) 1998,1999 AbiSource, Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "ut_types.h"
#include "ut_misc.h"
#include "ut_assert.h"
#include "ut_string.h"

UT_sint32 UT_strcmp(const char *s1, const char *s2)
{
	return strcmp(s1,s2);
}

UT_sint32 UT_stricmp(const char *s1, const char *s2)
{
	// Quick implementation of stricmp until someone finds the Mac equivalent in the Toolbox
	for(; toupper(*s1) == toupper(*s2); s1++, s2++)
	{
		if(*s1 == '\0' || (*s2 == '\0'))
			return 0;
	}
	return *s1 - *s2;
}

UT_sint32 UT_strnicmp(const char *s1, const char *s2, int lenS1)
{
	int len;
	// Quick implementation of strnicmp until someone finds the Mac equivalent in the Toolbox
	for(len = 0; len < lenS1 && toupper(*s1) == toupper(*s2); s1++, s2++, len++)
	{
		if(*s1 == '\0' || (*s2 == '\0'))
			return 0;
	}
	if(len == lenS1)
		return 0;
	else
		return *s1 - *s2;
}

char * UT_catPathname(const char * szPath, const char * szFile)
{
	UT_ASSERT((szPath) && (*szPath));
	UT_ASSERT((szFile) && (*szFile));
	
	char * szPathname = (char *)calloc(sizeof(char),strlen(szPath)+strlen(szFile)+2);
	UT_ASSERT(szPathname);
	
	sprintf(szPathname,"%s%s%s",
			szPath,
			((szPath[strlen(szPath)-1]=='\\') ? "" : "\\"),
			szFile);

	return szPathname;
}

