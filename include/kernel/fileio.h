#ifndef FILEIO_H
#define FILEIO_H

#include <kernel/types.h>

//Create directory
int create_dir(const char *parent, const char *name);

//Create file
int create_file(const char *parent, const char *name);

//Create a content file
int create_file_with_content(const char *parent, const char *name, const char *content);

//Write to file
int write_to_file(const char *path, const char *content);

//Read file
char* read_file(const char *path);

//Check existence
int file_exists(const char *path);
int dir_exists(const char *path);

//Delete file/directory
int delete_file(const char *path);
int delete_dir(const char *path);

//Get full path
char* make_path(const char *parent, const char *name);

#endif
