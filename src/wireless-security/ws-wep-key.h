/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2007 - 2009 Red Hat, Inc.
 */

#ifndef WS_WEP_KEY_H
#define WS_WEP_KEY_H

#include <nm-setting-wireless-security.h>

typedef struct {
	struct _WirelessSecurity parent;

	NMWepKeyType type;
	char keys[4][65];
	guint8 cur_index;
} WirelessSecurityWEPKey;

WirelessSecurityWEPKey * ws_wep_key_new (const char *glade_file,
                                         NMConnection *connection,
                                         NMWepKeyType type,
                                         gboolean adhoc_create,
                                         gboolean simple);

#endif /* WS_WEP_KEY_H */

