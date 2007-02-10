/*
* Description: find a file in a branch
*
* License: BSD-style license
*
*
* Author: Radek Podgorny <radek@podgorny.cz>
*
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statvfs.h>

#include "unionfs.h"
#include "opts.h"
#include "cache.h"
#include "stats.h"
#include "debug.h"
#include "hashtable.h"
#include "hash.h"

/**
 * Check if the given fname suffixes the hide tag
 */
static char *hide_tag(const char *fname)
{
	char *tag = strstr(fname, HIDETAG);

	// check if fname has tag, fname is not only the tag, file name ends with the tag
	// TODO: static strlen(HIDETAG)
	if (tag && tag != fname && strlen(tag) == strlen(HIDETAG)) {
		return tag;
	}

	return NULL;
}

/**
 * read a directory and add files with the hiddenflag to the 
 * list of hidden files
 * TODO: this should be handled in the readdir()'s loop but we'll
 * loose ability to hide files within the same dir. I don't see
 * a reason why unionfs would do this but I'm leaving it here for now...
 */
static void read_hides(struct hashtable *hides, DIR *dp)
{
	struct dirent *de;
	char *tag;
	
	while ((de = readdir(dp)) != NULL) {
		tag = hide_tag(de->d_name);
		if (tag) {
			// ignore this file
			hashtable_insert(hides, strdup(de->d_name), malloc(1));
			
			// even more important, ignore the file without the tag!
			// hint: tag is a pointer to the flag-suffix within de->d_name
			*tag = '\0';
			hashtable_insert(hides, strdup(de->d_name), malloc(1));
		}
	}
	rewinddir (dp);
}

/**
 * unionfs-fuse readdir function
 */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) 
{
	(void)offset;
	(void)fi;
	int i = 0;

	DBG("readdir\n");

	// we will store already added files here to handle same file names across different roots
	struct hashtable *files = create_hashtable(16, string_hash, string_equal);
	struct hashtable *hides = create_hashtable(16, string_hash, string_equal);

	for (i = 0; i < uopt.nroots; i++) {
		char p[PATHLEN_MAX];
		
		snprintf(p, PATHLEN_MAX, "%s%s", uopt.roots[i].path, path);

		DIR *dp = opendir(p);
		if (dp == NULL) continue;

		read_hides(hides, dp);

		struct dirent *de;
		while ((de = readdir(dp)) != NULL) {
			// already added in some other root
			if (hashtable_search(files, de->d_name) != NULL) continue;

			// file should be hidden from the user
			if (hashtable_search(hides, de->d_name) != NULL) continue;

			// fill with something dummy, we're interested in key existence only
			hashtable_insert(files, strdup(de->d_name), malloc(1));

			struct stat st;
			memset(&st, 0, sizeof(st));
			st.st_ino = de->d_ino;
			st.st_mode = de->d_type << 12;
			
			if (filler(buf, de->d_name, &st, 0)) break;
		}

		closedir(dp);
	}

	hashtable_destroy(files, 1);
	hashtable_destroy(hides, 1);
	
	if (uopt.stats_enabled && strcmp(path, "/") == 0) {
		filler(buf, "stats", NULL, 0);
	}

	return 0;
}
