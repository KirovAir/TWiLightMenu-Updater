#ifndef FILE_BROWSE_H
#define FILE_BROWSE_H

#include "download.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <vector>

using namespace std;

struct DirEntry {
	string name;
	string path;
	string sha1;
	bool isDirectory;
	off_t size;
};

typedef struct {
	char gameTitle[12];			//!< 12 characters for the game title.
	char gameCode[4];			//!< 4 characters for the game code.
} sNDSHeadertitlecodeonly;

void findCompatibleFiles(vector<DirEntry>& dirContents, std::string currentDir = "");

void getDirectoryContents (vector<DirEntry>& dirContents);

#endif //FILE_BROWSE_H
