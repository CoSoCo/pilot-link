/*
 * addresses.c:  Translate Palm address book into a generic format
 *
 * Copyright (c) 1996, Kenneth Albanowski
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "getopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <utime.h>

#include "pi-source.h"
#include "pi-socket.h"
#include "pi-address.h"
#include "pi-dlp.h"
#include "pi-header.h"

int pilot_connect(const char *port);
static void Help(char *progname);

struct option options[] = {
	{"help",     no_argument,       NULL, 'h'},
	{"port",     required_argument, NULL, 'p'},
	{"fancy",    no_argument,       NULL, 'f'},
	{NULL,       0,                 NULL, 0}
};

static const char *optstring = "hp:f";

int main(int argc, char *argv[])
{
	int c;
	int db;
	int i;
	int sd = -1;
	int fstyle = -1;
	struct AddressAppInfo aai;
	unsigned char buffer[0xffff];
	char *progname = argv[0];
	char *port = NULL;
	char *fancy = NULL;

        while ((c =
                getopt(argc, argv, optstring)) != -1) {
                switch (c) {

                  case 'h':
                          Help(progname);
                          exit(0);
                  case 'p':
                          port = optarg;
                          break;
                  case 'f':
                          fancy = optarg;
			  fstyle = 1;
                          break;
                  case ':':
                }
        }

        if (port == NULL) {
		PalmHeader(progname);
                Help(progname);
                printf("ERROR: You forgot to specify a valid port\n");
                exit(1);
        } else {
		
		sd = pilot_connect(port);
	
		/* Did we get a valid socket descriptor back? */
		if (dlp_OpenConduit(sd) < 0) {
			exit(1);
		}
	
		/* Tell user (via Palm) that we are starting things up */
		dlp_OpenConduit(sd);
	
		/* Open the Address book's database, store access handle in db */
		if (dlp_OpenDB(sd, 0, 0x80 | 0x40, "AddressDB", &db) < 0) {
			puts("Unable to open AddressDB");
			dlp_AddSyncLogEntry(sd, "Unable to open AddressDB.\n");
			exit(1);
		}
	
		dlp_ReadAppBlock(sd, db, 0, buffer, 0xffff);
		unpack_AddressAppInfo(&aai, buffer, 0xffff);
	
		for (i = 0;; i++) {
			struct Address a;
			int attr;
 			int category;
			int count = 0;
			int j;
	
			int len =
			    dlp_ReadRecordByIndex(sd, db, i, buffer, 0, 0, &attr,
						  &category);
	
			if (len < 0)
				break;
	
			/* Skip deleted records */
			if ((attr & dlpRecAttrDeleted)
			    || (attr & dlpRecAttrArchived))
				continue;
	
			unpack_Address(&a, buffer, len);

			if (fstyle == 1) {
				printf(".");
				for(count=0;count<50;count++) printf("-");
				printf(".\n");
				printf("| Category     %35s |\n", aai.category.name[category]);
			} else {
				printf("Category: %s\n", aai.category.name[category]);
			}
			for (j = 0; j < 19; j++) {
				if (a.entry[j]) {
					int l = j;
	
					if ((l >= entryPhone1) && (l <= entryPhone5))
						if (fstyle == 1) {
							printf("| %-11s: %-35s |\n", aai.phoneLabels[a.phoneLabel[l - entryPhone1]], a.entry[j]);
						} else {
							printf("%s: %s\n", aai.phoneLabels[a.phoneLabel[l - entryPhone1]], a.entry[j]);
						}
					else
						if (fstyle == 1) {
							printf("| %-11s: %-35s |\n", 
								aai.labels[l], a.entry[j]);
						} else {
							printf("%s: %s\n", 
								aai.labels[l], a.entry[j]);
						}
				}
			}
			if (fstyle == 1) {
				printf("`");
				for(count=0;count<50;count++) printf("-");
				printf("'\n");
			}
			printf("\n");
			free_Address(&a);
		}
	}

	/* Close the database */
	dlp_CloseDB(sd, db);

        dlp_AddSyncLogEntry(sd, "Successfully read addresses from Palm\nThank you for using pilot-link.\n");
	pi_close(sd);
	return 0;
}


static void Help(char *progname)
{
        printf("   Dumps the Palm AddressDB database into a generic text output format\n\n"
               "   Usage: %s -p <port> [options]\n"
               "   Only the port option is required, the other options are... optional.\n\n"
               "   -p <port>           Use device file <port> to communicate with Palm\n"
               "   -f                  Use the new \"fancy\" index card output format\n"
	       "   -h                  Display this information\n\n"
               "   Example: %s -p /dev/pilot\n\n"
	       "   You can redirect the output of %s to a file instead of the default\n"
	       "   STDOUT by using redirection and pipes as necessary.\n\n"
	       "   Example: %s -p /dev/pilot -f > MyAddresses.txt\n\n", progname, progname, progname, progname);
	return;
}
