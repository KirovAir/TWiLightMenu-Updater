#include "fileBrowse.h"
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <unistd.h>
#include "sha1.hpp"

using namespace std;

int file_count = 0;

extern bool continueNdsScan;

extern void displayBottomMsg(const char* text);

/**
 * Get the title ID.
 * @param ndsFile DS ROM image.
 * @param buf Output buffer for title ID. (Must be at least 4 characters.)
 * @return 0 on success; non-zero on error.
 */
int grabTID(FILE *ndsFile, char *buf) {
	fseek(ndsFile, offsetof(sNDSHeadertitlecodeonly, gameCode), SEEK_SET);
	size_t read = fread(buf, 1, 4, ndsFile);
	return !(read == 4);
}

bool hasBoxart(std::string fileName) {
	char boxartPath[256];
	snprintf(boxartPath, sizeof(boxartPath), "sdmc:/_nds/TWiLightMenu/boxart/%s.png", fileName.c_str());
	if(access(boxartPath, F_OK) != 0 || getFileSize(boxartPath) == 0) { // Can not access or size is 0.
		return false;
	}

	return true;
}

void findCompatibleFiles(vector<DirEntry>& dirContents, std::string currentDir="") {
	struct stat st;
	DIR *pdir = opendir(".");

	if (pdir == NULL) {
		displayBottomMsg("Unable to open the directory.");
		for(int i=0;i<120;i++)
			gspWaitForVBlank();
	} else {
		while (continueNdsScan)
		{
			DirEntry dirEntry;

			struct dirent* pent = readdir(pdir);
			if (pent == NULL) break;

			stat(pent->d_name, &st);
			dirEntry.name = pent->d_name;

			char scanningMessage[512];
			snprintf(scanningMessage, sizeof(scanningMessage), "Scanning SD card for roms...\n\n(Press B to cancel)\n\n\n\n\n\n\n\n\n%s", dirEntry.name.c_str());
			displayBottomMsg(scanningMessage);

			dirEntry.isDirectory = (st.st_mode & S_IFDIR) ? true : false;
				if(!(dirEntry.isDirectory) && dirEntry.name.length() >= 4) {
					bool isCompatible = false;
					bool grabSha1 = false;
					if (strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".nds") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-3, 3).c_str(), ".ds") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".dsi") == 0) {
						isCompatible = true;
					} else if (strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".nes") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".sfc") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".smc") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".sne") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-3, 3).c_str(), ".gb") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".sgb") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".gbc") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".gba") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".nds") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-3, 3).c_str(), ".gg") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".gen") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".sms") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".fds") == 0
					|| strcasecmp(dirEntry.name.substr(dirEntry.name.length()-4, 4).c_str(), ".bin") == 0) 
					{
						isCompatible = true;
						grabSha1 = true;
					}

					if (isCompatible && !hasBoxart(dirEntry.name)) {
						dirEntry.path = currentDir + "/" + dirEntry.name;

						if (grabSha1) { // TitleId not compatible. Use Sha1.
							dirEntry.sha1 = SHA1::from_file(dirEntry.path);
						}

						dirContents.push_back(dirEntry);
						file_count++;
					}
				} else if (dirEntry.isDirectory
				&& dirEntry.name.compare(".") != 0
				&& dirEntry.name.compare("_nds") != 0
				&& dirEntry.name.compare("3ds") != 0
				&& dirEntry.name.compare("DCIM") != 0
				&& dirEntry.name.compare("gm9") != 0
				&& dirEntry.name.compare("luma") != 0
				&& dirEntry.name.compare("Nintendo 3DS") != 0
				&& dirEntry.name.compare("private") != 0
				&& dirEntry.name.compare("retroarch") != 0) {
					chdir(dirEntry.name.c_str());
					findCompatibleFiles(dirContents, currentDir + "/" + dirEntry.name);
					chdir("..");
				}
		}
		closedir(pdir);
	}
}

off_t getFileSize(const char *fileName) {
	FILE* fp = fopen(fileName, "rb");
	off_t fsize = 0;
	if (fp) {
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);			// Get source file's size
		fseek(fp, 0, SEEK_SET);
	}
	fclose(fp);

	return fsize;
}

void getDirectoryContents (vector<DirEntry>& dirContents) {
	struct stat st;

	dirContents.clear();

	DIR *pdir = opendir (".");

	if (pdir == NULL) {
		displayBottomMsg("Unable to open the directory.");
		for(int i=0;i<120;i++)
			gspWaitForVBlank();
	} else {

		while(true) {
			DirEntry dirEntry;

			struct dirent* pent = readdir(pdir);
			if(pent == NULL) break;

			stat(pent->d_name, &st);
			if (strcmp(pent->d_name, "..") != 0) {
				dirEntry.name = pent->d_name;
				dirEntry.isDirectory = (st.st_mode & S_IFDIR) ? true : false;
				if (!dirEntry.isDirectory) {
					dirEntry.size = getFileSize(dirEntry.name.c_str());
				}

				if (dirEntry.name.compare(".") != 0) {
					dirContents.push_back (dirEntry);
				}
			}

		}

		closedir(pdir);
	}
}
