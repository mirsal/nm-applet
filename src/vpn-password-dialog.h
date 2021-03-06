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
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifndef VPN_PASSWORD_DIALOG_H
#define VPN_PASSWORD_DIALOG_H

#include <glib.h>
#include <nma-gconf-connection.h>

gboolean nma_vpn_request_password (NMSettingsConnectionInterface *connection,
                                   gboolean retry,
                                   NMANewSecretsRequestedFunc callback,
                                   gpointer callback_data);

#endif
