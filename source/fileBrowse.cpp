#include "fileBrowse.h"
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <unistd.h>
#include "base64.h"

using namespace std;

static std::string supportedExt [] = {
	".nds",
	".ds",
	".dsi",
	".nes", 
	".sfc", 
	".smc", 
	".snes", 
	".gb", 
	".sgb",
	".gbc",
	".gba",
	".gg",
	".gen",
	".sms",
	".fds",
	".bin"
};

extern bool continueNdsScan;

extern void displayBottomMsg(const char* text);

std::string getHeaderData(const char* path) {
	std::string headerData;
	FILE *f_nds_file = fopen(path, "rb");
	if (f_nds_file) {
		fseek(f_nds_file, 0, SEEK_END);
		off_t fsize = ftell(f_nds_file); // Get size
		fseek(f_nds_file, 0, SEEK_SET);
		if (fsize > 328) {
			BYTE header[328]; 
			fread(header, 1, 328, f_nds_file);
			headerData = base64_encode(header,sizeof(header));
		}
	}
	fclose(f_nds_file);

	return headerData;
}

bool hasBoxart(std::string fileName) {
	char boxartPath[256];
	snprintf(boxartPath, sizeof(boxartPath), "sdmc:/_nds/TWiLightMenu/boxart/%s.png", fileName.c_str());
	if(access(boxartPath, F_OK) != 0 || getFileSize(boxartPath) == 0) { // Can not access or size is 0.
		return false;
	}

	return true;
}

bool hasExtension(std::string fileName, std::string ext) {
	int extlen = ext.length();
	if (strcasecmp(fileName.substr(fileName.length() - extlen, extlen).c_str(), ext.c_str()) == 0) {
		return true;
	}
	return false;
}

bool isSupportedRom(std::string fileName) {
    for(auto& ext : supportedExt) {
        if (hasExtension(fileName, ext)) {								
			return true;
		}
    }
	return false;
}

void findCompatibleFiles(vector<DirEntry>& dirContents, std::string currentDir) {
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
					bool isCompatible = isSupportedRom(dirEntry.name);

					if (isCompatible) {
						dirEntry.path = currentDir + "/" + dirEntry.name;
						dirContents.push_back(dirEntry);
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
