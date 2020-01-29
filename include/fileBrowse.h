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

bool hasBoxart(std::string fileName);

void findCompatibleFiles(vector<DirEntry>& dirContents, std::string currentDir = "");

void getDirectoryContents (vector<DirEntry>& dirContents);

off_t getFileSize(const char *fileName);

#endif //FILE_BROWSE_H
