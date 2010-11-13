/* 
** Copyright (C) 2008-2010 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**  
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**  
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  
**  
*/

#ifndef __MU_MSG_PRIO_H__
#define __MU_MSG_PRIO_H__

enum _MuMsgPrio {  
	MU_MSG_PRIO_NONE    = 0,

	MU_MSG_PRIO_LOW     = 1,
	MU_MSG_PRIO_NORMAL  = 2,
	MU_MSG_PRIO_HIGH    = 3
};
typedef enum _MuMsgPrio MuMsgPrio;

#endif /*__MU_MSG_PRIO_H__*/
