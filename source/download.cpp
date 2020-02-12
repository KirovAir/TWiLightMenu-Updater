#include "download.hpp"
#include <fstream>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>

#include "extract.hpp"
#include "fileBrowse.h"
#include "gui.hpp"
#include "inifile.h"
#include "keyboard.h"
#include "thread.h"
#include "sha1.hpp"

extern "C" {
	#include "cia.h"
}

#define  USER_AGENT   APP_TITLE "-" VERSION_STRING

static char* result_buf = NULL;
static size_t result_sz = 0;
static size_t result_written = 0;
std::vector<std::string> _topText;
std::string jsonName;
CIniFile versionsFile("sdmc:/_nds/TWiLightMenu/extras/updater/currentVersionsV2.ini");
std::string latestMenuReleaseCache = "";
std::string latestMenuNightlyCache = "";
std::string latestBootstrapReleaseCache = "";
std::string latestBootstrapNightlyCache = "";
std::string latestUpdaterReleaseCache = "";
std::string latestUpdaterNightlyCache = "";
std::string usernamePasswordCache = "";

extern C3D_RenderTarget* top;
extern C3D_RenderTarget* bottom;

extern bool downloadNightlies;
extern bool updateAvailable[];
extern bool updated3dsx;
extern int filesExtracted;
extern std::string extractingFile;

char progressBarMsg[64] = "";
bool showProgressBar = false;
bool progressBarType = 0; // 0 = Download | 1 = Extract
bool continueNdsScan = true;

// following function is from
// https://github.com/angelsl/libctrfgh/blob/master/curl_test/src/main.c
static size_t handle_data(char* ptr, size_t size, size_t nmemb, void* userdata) {
	(void) userdata;
	const size_t bsz = size*nmemb;

	if(result_sz == 0 || !result_buf) {
		result_sz = 0x1000;
		result_buf = (char*)malloc(result_sz);
	}

	bool need_realloc = false;
	while (result_written + bsz > result_sz) {
		result_sz <<= 1;
		need_realloc = true;
	}

	if(need_realloc) {
		char *new_buf = (char*)realloc(result_buf, result_sz);
		if(!new_buf)
		{
			return 0;
		}
		result_buf = new_buf;
	}

	if(!result_buf) {
		return 0;
	}

	memcpy(result_buf + result_written, ptr, bsz);
	result_written += bsz;
	return bsz;
}

static Result setupContext(CURL *hnd, const char * url) {
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, handle_data);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(hnd, CURLOPT_STDERR, stdout);
	if(usernamePasswordCache != "")	curl_easy_setopt(hnd, CURLOPT_USERPWD, usernamePasswordCache.c_str());

	return 0;
}

Result downloadToFile(std::string url, std::string path) {
	Result ret = 0;
	printf("Downloading from:\n%s\nto:\n%s\n", url.c_str(), path.c_str());
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return -1;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return ret;
	}

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, url.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return ret;
	}

	Handle fileHandle;
	u64 offset = 0;
	u32 bytesWritten = 0;

	ret = openFile(&fileHandle, path.c_str(), true);
	if(R_FAILED(ret)) {
		printf("Error: couldn't open file to write.\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return DL_ERROR_WRITEFILE;
	}

	CURLcode cres = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);

	if(cres != CURLE_OK) {
		printf("Error in:\ncurl\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return -1;
	}

	FSFILE_Write(fileHandle, &bytesWritten, offset, result_buf, result_written, 0);

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;
	FSFILE_Close(fileHandle);
	return 0;
}

Result downloadBoxartToFile(std::string filename, std::string sha1, std::string header, std::string targetPath, int boxartSize, int borderStyle) {
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return -1;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return ret;
	}

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, "http://boxart.kirovair.com/api");
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return ret;
	}

	Handle fileHandle;
	u64 offset = 0;
	u32 bytesWritten = 0;

	ret = openFile(&fileHandle, targetPath.c_str(), true);
	if(R_FAILED(ret)) {
		displayBottomMsg("Error: couldn't open file to write.\n");
		sleep(5);
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return DL_ERROR_WRITEFILE;
	}

	int boxartWidth = 128;
	int boxartHeight = 115;
	if (boxartSize == 1) {
		boxartWidth = 210;
		boxartHeight = 146;
	} else if (boxartSize == 2) {
		boxartWidth = 256;
		boxartHeight = 192;
	}

	std::string borderStyleStr = "None";
	if (borderStyle == 1 || borderStyle == 2) {
		borderStyleStr = "Line";
		if (borderStyle == 2) {
			borderStyleStr += "&BoxartBorderColor=0xFFFFFFFF";
		}
	} else if (borderStyle == 3) {
		borderStyleStr = "NintendoDSi";
	}

	std::string formdata =	"Filename=" + std::string(curl_easy_escape(hnd, filename.c_str(), filename.size())) + 
							"&Sha1=" + sha1 + 
							"&Header=" + std::string(curl_easy_escape(hnd, header.c_str(), header.size())) +
							"&BoxartWidth=" + std::to_string(boxartWidth) +
							"&BoxartHeight=" + std::to_string(boxartHeight) + 
							"&BoxartBorderStyle=" + borderStyleStr;
	
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, formdata.c_str());

	CURLcode cres = curl_easy_perform(hnd);
	long response_code = 0;
	if(cres == CURLE_OK) {
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &response_code);
	}
	curl_easy_cleanup(hnd);

	if(cres != CURLE_OK) {
		displayBottomMsg("Error in:\ncurl\n");
		sleep(5);
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return -1;
	}

	if (response_code != 200) {
		char downloadMessage[512];
		snprintf(downloadMessage, sizeof(downloadMessage), "Downloading\n%s.png\nNothing found! (Code: %ld)", filename.c_str(), response_code);
		displayBottomMsg(downloadMessage);
		sleep(1);
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return 0;
	}

	FSFILE_Write(fileHandle, &bytesWritten, offset, result_buf, result_written, 0);

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;
	FSFILE_Close(fileHandle);
	return 0;
}

Result downloadFromRelease(std::string url, std::string asset, std::string path) {
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return -1;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return ret;
	}

	std::regex parseUrl("github\\.com\\/(.+)\\/(.+)");
	std::smatch result;
	regex_search(url, result, parseUrl);

	std::string repoOwner = result[1].str(), repoName = result[2].str();

	std::stringstream apiurlStream;
	apiurlStream << "https://api.github.com/repos/" << repoOwner << "/" << repoName << "/releases/latest";
	std::string apiurl = apiurlStream.str();

	printf("Downloading latest release from repo:\n%s\nby:\n%s\n", repoName.c_str(), repoOwner.c_str());
	printf("Crafted API url:\n%s\n", apiurl.c_str());

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, apiurl.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return ret;
	}

	CURLcode cres = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);
	char* newbuf = (char*)realloc(result_buf, result_written + 1);
	result_buf = newbuf;
	result_buf[result_written] = 0; //nullbyte to end it as a proper C style string

	if(cres != CURLE_OK) {
		printf("Error in:\ncurl\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return -1;
	}

	printf("Looking for asset with name matching:\n%s\n", asset.c_str());
	std::string assetUrl;
	json parsedAPI = json::parse(result_buf);
	if(parsedAPI["assets"].is_array()) {
		for (auto jsonAsset : parsedAPI["assets"]) {
			if(jsonAsset.is_object() && jsonAsset["name"].is_string() && jsonAsset["browser_download_url"].is_string()) {
				std::string assetName = jsonAsset["name"];
				if(matchPattern(asset, assetName)) {
					assetUrl = jsonAsset["browser_download_url"];
					break;
				}
			}
		}
	}
	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;

	if(assetUrl.empty())
		ret = DL_ERROR_GIT;
	else
		ret = downloadToFile(assetUrl, path);

	return ret;
}

/**
 * Check Wi-Fi status.
 * @return True if Wi-Fi is connected; false if not.
 */
bool checkWifiStatus(void) {
	return true; // Citra
	u32 wifiStatus;
	bool res = false;

	if(R_SUCCEEDED(ACU_GetWifiStatus(&wifiStatus)) && wifiStatus) {
		res = true;
	}

	return res;
}

void downloadFailed(void) {
	displayBottomMsg("Download failed!");
	for (int i = 0; i < 60*2; i++) {
		gspWaitForVBlank();
	}
}

void notConnectedMsg(void) {
	displayBottomMsg("Please connect to Wi-Fi");
	for (int i = 0; i < 60*2; i++) {
		gspWaitForVBlank();
	}
}

void doneMsg(void) {
	displayBottomMsg("Done!");
	for (int i = 0; i < 60*2; i++) {
		gspWaitForVBlank();
	}
}

std::string getLatestRelease(std::string repo, std::string item, bool retrying) {
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return "";
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return "";
	}

	std::stringstream apiurlStream;
	apiurlStream << "https://api.github.com/repos/" << repo << "/releases/latest";
	std::string apiurl = apiurlStream.str();

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, apiurl.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return "";
	}

	CURLcode cres = curl_easy_perform(hnd);
	int httpCode;
	curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(hnd);
	char* newbuf = (char*)realloc(result_buf, result_written + 1);
	result_buf = newbuf;
	result_buf[result_written] = 0; //nullbyte to end it as a proper C style string

	if(httpCode == 401 || httpCode == 403) {
		bool save = promtUsernamePassword();
		if(save)	saveUsernamePassword();
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		if(usernamePasswordCache == "" || retrying) {
			usernamePasswordCache = "";
			return "API";
		} else {
			return getLatestRelease(repo, item, true);
		}
	}

	if(cres != CURLE_OK) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return "";
	}


	std::string jsonItem;
	json parsedAPI = json::parse(result_buf);
	if(parsedAPI[item].is_string()) {
		jsonItem = parsedAPI[item];
	}

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;

	return jsonItem;
}

std::vector<std::string> getRecentCommits(std::string repo, std::string item, bool retrying) {
	std::vector<std::string> emptyVector;
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return emptyVector;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return emptyVector;
	}

	std::stringstream apiurlStream;
	apiurlStream << "https://api.github.com/repos/" << repo << "/commits";
	std::string apiurl = apiurlStream.str();

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, apiurl.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return emptyVector;
	}

	CURLcode cres = curl_easy_perform(hnd);
	int httpCode;
	curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(hnd);
	char* newbuf = (char*)realloc(result_buf, result_written + 1);
	result_buf = newbuf;
	result_buf[result_written] = 0; //nullbyte to end it as a proper C style string

	if(httpCode == 401 || httpCode == 403) {
		bool save = promtUsernamePassword();
		if(save)	saveUsernamePassword();
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		if(usernamePasswordCache == "" || retrying) {
			usernamePasswordCache = "";
			return {"API"};
		} else {
			return getRecentCommits(repo, item, true);
		}
	}

	if(cres != CURLE_OK) {
		printf("Error in:\ncurl\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return emptyVector;
	}

	std::vector<std::string> jsonItems;
	json parsedAPI = json::parse(result_buf);
	for(uint i=0;i<parsedAPI.size();i++) {
		if(parsedAPI[i][item].is_string()) {
			jsonItems.push_back(parsedAPI[i][item]);
		}
	}

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;

	return jsonItems;
}

std::vector<std::string> getRecentCommitsArray(std::string repo, std::string array, std::string item, bool retrying) {
	std::vector<std::string> emptyVector;
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	if(!socubuf) {
		return emptyVector;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return emptyVector;
	}

	std::stringstream apiurlStream;
	apiurlStream << "https://api.github.com/repos/" << repo << "/commits";
	std::string apiurl = apiurlStream.str();

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, apiurl.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return emptyVector;
	}

	CURLcode cres = curl_easy_perform(hnd);
	int httpCode;
	curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(hnd);
	char* newbuf = (char*)realloc(result_buf, result_written + 1);
	result_buf = newbuf;
	result_buf[result_written] = 0; //nullbyte to end it as a proper C style string

	if(httpCode == 401 || httpCode == 403) {
		bool save = promtUsernamePassword();
		if(save)	saveUsernamePassword();
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		if(usernamePasswordCache == "" || retrying) {
			usernamePasswordCache = "";
			return {"API"};
		} else {
			return getRecentCommitsArray(repo, array, item, true);
		}
	}

	if(cres != CURLE_OK) {
		printf("Error in:\ncurl\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return emptyVector;
	}

	std::vector<std::string> jsonItems;
	json parsedAPI = json::parse(result_buf);
	for(uint i=0;i<parsedAPI.size();i++) {
		if(parsedAPI[i][array][item].is_string()) {
			jsonItems.push_back(parsedAPI[i][array][item]);
		}
	}

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;

	return jsonItems;
}

std::vector<ThemeEntry> getThemeList(std::string repo, std::string path, bool retrying) {
	Result ret = 0;
	void *socubuf = memalign(0x1000, 0x100000);
	std::vector<ThemeEntry> emptyVector;
	if(!socubuf) {
		return emptyVector;
	}

	ret = socInit((u32*)socubuf, 0x100000);
	if(R_FAILED(ret)) {
		free(socubuf);
		return emptyVector;
	}

	std::stringstream apiurlStream;
	apiurlStream << "https://api.github.com/repos/" << repo << "/contents/" << path;
	std::string apiurl = apiurlStream.str();

	CURL *hnd = curl_easy_init();
	ret = setupContext(hnd, apiurl.c_str());
	if(ret != 0) {
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		return emptyVector;
	}

	CURLcode cres = curl_easy_perform(hnd);
	int httpCode;
	curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(hnd);
	char* newbuf = (char*)realloc(result_buf, result_written + 1);
	result_buf = newbuf;
	result_buf[result_written] = 0; //nullbyte to end it as a proper C style string

	if(httpCode == 401 || httpCode == 403) { // Unauthorized || Forbidden
		bool save = promtUsernamePassword();
		if(save)	saveUsernamePassword();
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;
		if(usernamePasswordCache == "" || retrying) {
			usernamePasswordCache = "";
			return {{"API", "API", "API", "API"}};
		} else {
			return getThemeList(repo, path, true);
		}
	}

	if(cres != CURLE_OK) {
		printf("Error in:\ncurl\n");
		socExit();
		free(result_buf);
		free(socubuf);
		result_buf = NULL;
		result_sz = 0;
		result_written = 0;

		return emptyVector;
	}

	std::vector<ThemeEntry> jsonItems;
	json parsedAPI = json::parse(result_buf);
	for(uint i=0;i<parsedAPI.size();i++) {
		ThemeEntry themeEntry;
		if(parsedAPI[i]["name"].is_string()) {
			themeEntry.name = parsedAPI[i]["name"];
		}
		if(parsedAPI[i]["download_url"].is_string()) {
			themeEntry.downloadUrl = parsedAPI[i]["download_url"];
		}
		if(parsedAPI[i]["path"].is_string()) {
			themeEntry.sdPath = "sdmc:/";
			themeEntry.sdPath += parsedAPI[i]["path"];
			themeEntry.path = parsedAPI[i]["path"];

			size_t pos;
			while ((pos = themeEntry.path.find(" ")) != std::string::npos) {
				themeEntry.path.replace(pos, 1, "%20");
			}
		}
		jsonItems.push_back(themeEntry);
	}

	socExit();
	free(result_buf);
	free(socubuf);
	result_buf = NULL;
	result_sz = 0;
	result_written = 0;

	return jsonItems;
}

void downloadTheme(std::string path) {
	vector<ThemeEntry> themeContents = getThemeList("DS-Homebrew/twlmenu-extras", path);
	for(uint i=0;i<themeContents.size();i++) {
		if(themeContents[i].downloadUrl != "") {
			displayBottomMsg(("Downloading: "+themeContents[i].name).c_str());
			downloadToFile(themeContents[i].downloadUrl, themeContents[i].sdPath);
		} else {
			displayBottomMsg(("Downloading: "+themeContents[i].name).c_str());
			mkdir((themeContents[i].sdPath).c_str(), 0777);
			downloadTheme(themeContents[i].path);
		}
	}
}

bool showReleaseInfo(std::string repo, bool showExitText) {
	displayBottomMsg("Loading release notes...");
	jsonName = getLatestRelease(repo, "name");
	std::string jsonBody = getLatestRelease(repo, "body");

	setMessageText(jsonBody);
	int textPosition = 0;
	bool redrawText = true;

	while(1) {
		if(redrawText) {
			drawMessageText(textPosition, showExitText);
			redrawText = false;
		}

		gspWaitForVBlank();
		hidScanInput();
		const u32 hDown = hidKeysDown();
		const u32 hHeld = hidKeysHeld();

		if(hHeld & KEY_UP || hHeld & KEY_DOWN) {
			for(int i=0;i<9;i++)
				gspWaitForVBlank();
		}

		if(hDown & KEY_A) {
			return true;
		} else if(hDown & KEY_B || hDown & KEY_Y || hDown & KEY_TOUCH) {
			return false;
		} else if(hHeld & KEY_UP) {
			if(textPosition > 0) {
				textPosition--;
				redrawText = true;
			}
		} else if(hHeld & KEY_DOWN) {
			if(textPosition < (int)(_topText.size() - 10)) {
				textPosition++;
				redrawText = true;
			}
		}
	}
}

std::string chooseCommit(std::string repo, std::string title, bool showExitText) {
	displayBottomMsg("Loading commits...");
	std::vector<std::string> jsonShasTemp = getRecentCommits(repo, "sha");
	std::vector<std::string> jsonBodyTemp = ((jsonShasTemp[0] == "API") ? jsonShasTemp : getRecentCommitsArray(repo, "commit", "message"));
	std::vector<std::string> jsonShas;
	std::vector<std::string> jsonBody;
	gspWaitForVBlank();

	jsonBody.push_back("Latest");
	jsonShas.push_back("master");

	if(jsonShasTemp[0] != "API"){
		for(uint i=0;i<jsonShasTemp.size();i++) {
			if(jsonBodyTemp[i].substr(0, title.size()) == title) {
				jsonBody.push_back(jsonBodyTemp[i]);
				jsonShas.push_back(jsonShasTemp[i]);
			}
		}
	}

	while (1) {
		u32 hDown;
		int selectedCommit = displayMenu("Choose a commit for commit notes:", jsonBody, "B: Back   A: Info", hDown);
		if (selectedCommit == -1) {
			return "";
		}

		setMessageText(jsonBody[selectedCommit]);
		jsonName = jsonShas[selectedCommit].substr(0,7);
		int textPosition = 0;
		bool redrawText = true;

		while(1) {
			if(redrawText) {
				drawMessageText(textPosition, showExitText);
				redrawText = false;
			}

			gspWaitForVBlank();
			hidScanInput();
			hDown = hidKeysDown();
			const u32 hHeld = hidKeysHeld();

			if(hHeld & KEY_UP || hHeld & KEY_DOWN) {
				for(int i=0;i<9;i++)
					gspWaitForVBlank();
			}

			if(hDown & KEY_A) {
				return jsonShas[selectedCommit].substr(0,7);
			} else if(hDown & KEY_B || hDown & KEY_Y || hDown & KEY_TOUCH) {
				break;
			} else if(hHeld & KEY_UP) {
				if(textPosition > 0) {
					textPosition--;
					redrawText = true;
				}
			} else if(hHeld & KEY_DOWN) {
				if(textPosition < (int)(_topText.size() - 10)) {
					textPosition++;
					redrawText = true;
				}
			}
		}
	}	
}

void setMessageText(const std::string &text) {
	std::string _topTextStr(text);
	std::vector<std::string> words;
	std::size_t pos;
	// std::replace( _topTextStr.begin(), _topTextStr.end(), '\n', ' ');
	_topTextStr.erase(std::remove(_topTextStr.begin(), _topTextStr.end(), '\r'), _topTextStr.end());
	while((pos = _topTextStr.find(' ')) != std::string::npos) {
		words.push_back(_topTextStr.substr(0, pos));
		_topTextStr = _topTextStr.substr(pos + 1);
	}
	if(_topTextStr.size())
		words.push_back(_topTextStr);
	std::string temp;
	_topText.clear();
	for(auto word : words) {
		int width = Draw_GetTextWidth(0.5f, (temp + " " + word).c_str());
		if(word.find('\n') != -1u)
		{
			word.erase(std::remove(word.begin(), word.end(), '\n'), word.end());
			temp += " " + word;
			_topText.push_back(temp);
			temp = "";
		}
		else if(width > 256)
		{
			_topText.push_back(temp);
			temp = word;
		}
		else
		{
			temp += " " + word;
		}
	}
	if(temp.size())
		_topText.push_back(temp);
}

void drawMessageText(int position, bool showExitText) {
	Gui::clearTextBufs();
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(bottom, TRANSPARENT);
	set_screen(bottom);
	Gui::sprite(sprites_BS_loading_background_idx, 0, 0);
	Draw_Text(18, 24, .7, BLACK, jsonName.c_str());
	for (int i = 0; i < (int)_topText.size() && i < (showExitText ? 9 : 10); i++) {
		Draw_Text(24, ((i * 16) + 48), 0.5f, BLACK, _topText[i+position].c_str());
	}
	if(showExitText)
		Draw_Text(24, 200, 0.5f, BLACK, "B: Cancel   A: Update");
	Draw_EndFrame();
}

void displayProgressBar() {
	char str[256];
	while(showProgressBar) {
		snprintf(str, sizeof(str), "%s\n%s%s\n%i %s", progressBarMsg, (!progressBarType ? "" : "\nCurrently extracting:\n"), (!progressBarType ? "" : extractingFile.c_str()), (!progressBarType ? (int)round(result_written/1000) : filesExtracted), (!progressBarType ? "KB downloaded." : (filesExtracted == 1 ? "file extracted." :"files extracted.")));
		displayBottomMsg(str);
		gspWaitForVBlank();
	}
}

bool promtUsernamePassword(void) {
	displayBottomMsg("The GitHub API rate limit has been\n"
					 "exceeded for your IP, you can regain\n"
					 "access by signing in to a GitHub account\n"
					 "or waiting for a bit\n"
					 "(or press B but some things won't work)\n\n\n\n\n\n\n\n"
					 "B: Cancel   A: Authenticate");

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	set_screen(bottom);
	Draw_Text(20, 100, 0.45f, BLACK, "Username:");
	Draw_Rect(100, 100, 100, 14, GRAY);
	Draw_Text(20, 120, 0.45f, BLACK, "Password:");
	Draw_Rect(100, 120, 100, 14, GRAY);

	Draw_Rect(100, 140, 14, 14, GRAY);
	Draw_Text(120, 140, 0.45f, BLACK, "Save login?");
	Draw_EndFrame();

	bool save = false;
	int hDown;
	std::string username, password;
	while(1) {
		do {
			gspWaitForVBlank();
			hidScanInput();
			hDown = hidKeysDown();
		} while(!hDown);

		if(hDown & KEY_A) {
			usernamePasswordCache = username + ":" + password;
			return save;
		} else if(hDown & KEY_B) {
			usernamePasswordCache = "";
			return save;
		} else if(hDown & KEY_TOUCH) {
			touchPosition touch;
			touchRead(&touch);
			if(touch.px >= 100 && touch.px <= 200 && touch.py >= 100 && touch.py <= 114) {
				username = keyboardInput("Username");
				C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
				set_screen(bottom);
				Draw_Rect(100, 100, 100, 14, GRAY);
				Draw_Text(100, 100, 0.45f, BLACK, username.c_str());
				Draw_EndFrame();
			} else if(touch.px >= 100 && touch.px <= 200 && touch.py >= 120 && touch.py <= 134) {
				password = keyboardInput("Password");
				C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
				set_screen(bottom);
				Draw_Rect(100, 120, 100, 14, GRAY);
				Draw_Text(100, 120, 0.45f, BLACK, password.c_str());
				Draw_EndFrame();
			} else if(touch.px >= 100 && touch.px <= 114 && touch.py >= 140 && touch.py <= 154) {
				save = !save;
				C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
				set_screen(bottom);
				Draw_Rect(102, 142, 10, 10, save ? GREEN : GRAY);
				Draw_EndFrame();
			}
		}
	}
}

void loadUsernamePassword(void) {
	std::ifstream in("sdmc:/_nds/TWiLightMenu/extras/updater/usernamePassword");
	if(in.good())	in >> usernamePasswordCache;
	in.close();
}

void saveUsernamePassword() {
	std::ofstream out("sdmc:/_nds/TWiLightMenu/extras/updater/usernamePassword");
	if(out.good())	out << usernamePasswordCache;
	out.close();
}

std::string latestMenuRelease(void) {
	if(latestMenuReleaseCache == "") {
		std::string apiInfo = getLatestRelease("DS-Homebrew/TWiLightMenu", "tag_name");
		if(apiInfo != "API") {
			latestMenuReleaseCache = apiInfo;
		}
	}
	return latestMenuReleaseCache;
}

std::string latestMenuNightly(void) {
	if(latestMenuNightlyCache == "") {
		std::string apiInfo = getRecentCommits("DS-Homebrew/TWiLightMenu", "sha")[0];
		if(apiInfo != "API") {
			latestMenuNightlyCache = apiInfo.substr(0,7);
		}
	}
	return latestMenuNightlyCache;
}

std::string latestBootstrapRelease(void) {
	if(latestBootstrapReleaseCache == "") {
		std::string apiInfo = getLatestRelease("ahezard/nds-bootstrap", "tag_name");
		if(apiInfo != "API") {
			latestBootstrapReleaseCache = apiInfo;
		}
	}
	return latestBootstrapReleaseCache;
}

std::string latestBootstrapNightly(void) {
	if(latestBootstrapNightlyCache == "") {
		std::string apiInfo = getRecentCommits("ahezard/nds-bootstrap", "sha")[0];
		if(apiInfo != "API") {
			latestBootstrapNightlyCache = apiInfo.substr(0,7);
		}
	}
	return latestBootstrapNightlyCache;
}

std::string latestUpdaterRelease(void) {
	if(latestUpdaterReleaseCache == "") {
		std::string apiInfo = getLatestRelease("RocketRobz/TWiLightMenu-Updater", "tag_name");
		if(apiInfo != "API") {
			latestUpdaterReleaseCache = apiInfo;
		}
	}
	return latestUpdaterReleaseCache;
}

std::string latestUpdaterNightly(void) {
	if(latestUpdaterNightlyCache == "") {
		std::string apiInfo = getRecentCommits("RocketRobz/TWiLightMenu-Updater", "sha")[0];
		if(apiInfo != "API") {
			latestUpdaterNightlyCache = apiInfo.substr(0,7);
		}
	}
	return latestUpdaterNightlyCache;
}

void saveUpdateData(void) {
	versionsFile.SaveIniFile("sdmc:/_nds/TWiLightMenu/extras/updater/currentVersionsV2.ini");
}

std::string getInstalledVersion(std::string component) {
	return versionsFile.GetString(component, "VERSION", "");
}

std::string getInstalledChannel(std::string component) {
	return versionsFile.GetString(component, "CHANNEL", "");
}

void setInstalledVersion(std::string component, std::string version) {
	versionsFile.SetString(component, "VERSION", version);
}

void setInstalledChannel(std::string component, std::string channel) {
	versionsFile.SetString(component, "CHANNEL", channel);
}

void checkForUpdates() {
	// First remove the old versions file
	unlink("sdmc:/_nds/TWiLightMenu/extras/updater/currentVersions.ini");

	std::string menuChannel = getInstalledChannel("TWILIGHTMENU");
	std::string menuVersion = getInstalledVersion("TWILIGHTMENU");
	std::string updaterChannel = getInstalledChannel("TWILIGHTMENU-UPDATER");
	std::string updaterVersion = getInstalledVersion("TWILIGHTMENU-UPDATER");
	std::string bootstrapRelease = getInstalledVersion("NDS-BOOTSTRAP-RELEASE");
	std::string boostrapNightly = getInstalledVersion("NDS-BOOTSTRAP-NIGHTLY");

	if(menuChannel == "release")
		updateAvailable[0] = menuVersion != latestMenuRelease();
	else if(menuChannel == "nightly")
		updateAvailable[1] = menuVersion != latestMenuNightly();
	else
		updateAvailable[0] = updateAvailable[1] = true;

	updateAvailable[2] = bootstrapRelease != latestBootstrapRelease();
	updateAvailable[3] = boostrapNightly != latestBootstrapNightly();

	if(updaterChannel == "release")
		updateAvailable[4] = updaterVersion != latestUpdaterRelease();
	else if(updaterChannel == "nightly")
		updateAvailable[5] = updaterVersion != latestUpdaterNightly();
	else
		updateAvailable[4] = updateAvailable[5] = true;
}

void updateBootstrap(std::string commit) {
	if(commit != "") {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading nds-bootstrap...\n(Nightly)");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadToFile("https://github.com/TWLBot/Builds/blob/"+commit+"/nds-bootstrap.7z?raw=true", "/nds-bootstrap-nightly.7z") != 0) {
			showProgressBar = false;
			downloadFailed();
			return;
		}

		snprintf(progressBarMsg, sizeof(progressBarMsg), "Extracting nds-bootstrap...\n(Nightly)");
		filesExtracted = 0;
		progressBarType = 1;
		extractArchive("/nds-bootstrap-nightly.7z", "nds-bootstrap/", "/_nds/");
		showProgressBar = false;

		deleteFile("sdmc:/nds-bootstrap-nightly.7z");

		setInstalledVersion("NDS-BOOTSTRAP-NIGHTLY", latestBootstrapNightly());
		saveUpdateData();
		updateAvailable[3] = false;
	} else {
		displayBottomMsg("Downloading nds-bootstrap...\n(Release)");
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading nds-bootstrap...\n(Release)");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadFromRelease("https://github.com/ahezard/nds-bootstrap", "nds-bootstrap\\.zip", "/nds-bootstrap-release.zip") != 0) {
			showProgressBar = false;
			downloadFailed();
			return;
		}

		snprintf(progressBarMsg, sizeof(progressBarMsg), "Extracting nds-bootstrap...\n(Release)");
		filesExtracted = 0;
		progressBarType = 1;
		extractArchive("/nds-bootstrap-release.zip", "/", "/_nds/");
		showProgressBar = false;

		deleteFile("sdmc:/nds-bootstrap-release.zip");

		setInstalledVersion("NDS-BOOTSTRAP-RELEASE", latestBootstrapRelease());
		saveUpdateData();
		updateAvailable[2] = false;
	}
	doneMsg();
}

void updateTWiLight(std::string commit) {
	if(commit != "") {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++...\n(Nightly)");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadToFile("https://github.com/TWLBot/Builds/blob/"+commit+"/TWiLightMenu.7z?raw=true", "/TWiLightMenu-nightly.7z") != 0) {
			showProgressBar = false;
			downloadFailed();
			return;
		}

		snprintf(progressBarMsg, sizeof(progressBarMsg), "Extracting TWiLight Menu++...\n(Nightly)");
		filesExtracted = 0;
		progressBarType = 1;
		extractArchive("/TWiLightMenu-nightly.7z", "TWiLightMenu/_nds/", "/_nds/");
		extractArchive("/TWiLightMenu-nightly.7z", "3DS - CFW users/", "/");
		showProgressBar = false;

		displayBottomMsg("Installing TWiLight Menu++ CIA...\n"
						"(Nightly)");
		installCia("/TWiLight Menu.cia");
		installCia("/TWiLight Menu - Game booter.cia");

		deleteFile("sdmc:/TWiLightMenu-nightly.7z");
		deleteFile("sdmc:/TWiLight Menu.cia");
		deleteFile("sdmc:/TWiLight Menu - Game booter.cia");

		setInstalledChannel("TWILIGHTMENU", "nightly");
		setInstalledVersion("TWILIGHTMENU", latestMenuNightly());
		saveUpdateData();
		updateAvailable[1] = false;
	} else {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++...\n(Release)");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadFromRelease("https://github.com/DS-Homebrew/TWiLightMenu", "TWiLightMenu\\.7z", "/TWiLightMenu-release.7z") != 0) {
			showProgressBar = false;
			downloadFailed();
			return;
		}

		snprintf(progressBarMsg, sizeof(progressBarMsg), "Extracting TWiLight Menu++...\n(Release)");
		filesExtracted = 0;
		progressBarType = 1;
		extractArchive("/TWiLightMenu-release.7z", "_nds/", "/_nds/");
		extractArchive("/TWiLightMenu-release.7z", "3DS - CFW users/", "/");
		extractArchive("/TWiLightMenu-release.7z", "DSi&3DS - SD card users/", "/");
		showProgressBar = false;

		displayBottomMsg("Installing TWiLight Menu++ CIA...\n"
						"(Release)");
		installCia("/TWiLight Menu.cia");
		installCia("/TWiLight Menu - Game booter.cia");

		deleteFile("sdmc:/TWiLightMenu-release.7z");
		deleteFile("sdmc:/TWiLight Menu.cia");
		deleteFile("sdmc:/TWiLight Menu - Game booter.cia");

		setInstalledChannel("TWILIGHTMENU", "release");
		setInstalledVersion("TWILIGHTMENU", latestMenuRelease());
		saveUpdateData();
		updateAvailable[0] = false;
	}
	doneMsg();
}

void updateSelf(std::string commit) {
	if(commit != "" && (access("sdmc:/3ds/TWiLightMenu-Updater.3dsx", F_OK) != 0)) {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++ Updater...\n(Nightly (cia))");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadToFile("https://github.com/TWLBot/Builds/blob/"+commit+"/TWiLightMenu%20Updater/TWiLight_Menu++_Updater.cia?raw=true", "/TWiLightMenu-Updater-nightly.cia") != 0) {
			downloadFailed();
			return;
		}
		showProgressBar = false;

		// Before the cia install because rebooting the app might've been causing this file to get cleared
		setInstalledChannel("TWILIGHTMENU-UPDATER", "nightly");
		setInstalledVersion("TWILIGHTMENU-UPDATER", latestUpdaterNightly());
		saveUpdateData();
		updateAvailable[5] = false;

		displayBottomMsg("Installing TWiLight Menu++ Updater CIA...\n"
						"(Nightly)\n\n\n\n\n\n\n\n\n\n"
						"The app will reboot when done.");
		installCia("/TWiLightMenu-Updater-nightly.cia");

		deleteFile("sdmc:/TWiLightMenu-Updater-nightly.cia");
	} else if(commit == "" && (access("sdmc:/3ds/TWiLightMenu-Updater.3dsx", F_OK) != 0)) {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++ Updater...\n(Release (cia))");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadFromRelease("https://github.com/RocketRobz/TWiLightMenu-Updater", "TWiLightMenu-Updater\\.cia", "/TWiLightMenu-Updater-release.cia") != 0) {
			downloadFailed();
			return;
		}
		showProgressBar = false;

		// Before the cia install because rebooting the app might've been causing this file to get cleared
		setInstalledChannel("TWILIGHTMENU-UPDATER", "release");
		setInstalledVersion("TWILIGHTMENU-UPDATER", latestUpdaterRelease());
		saveUpdateData();
		updateAvailable[4] = false;

		displayBottomMsg("Installing TWiLight Menu++ Updater CIA...\n"
						"(Release)\n\n\n\n\n\n\n\n\n\n"
						"The app will reboot when done.");
		installCia("/TWiLightMenu-Updater-release.cia");

		deleteFile("sdmc:/TWiLightMenu-Updater-release.cia");
	} else if(commit != "" && (access("sdmc:/3ds/TWiLightMenu-Updater.3dsx", F_OK) == 0)) {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++ Updater...\n(Nightly (3dsx))");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadToFile("https://github.com/TWLBot/Builds/blob/"+commit+"/TWiLightMenu%20Updater/TWiLight_Menu++_Updater.3dsx?raw=true", "/3ds/TWiLightMenu-Updater.3dsx") != 0) {
			downloadFailed();
			return;
		}
		showProgressBar = false;

		setInstalledChannel("TWILIGHTMENU-UPDATER", "nightly");
		setInstalledVersion("TWILIGHTMENU-UPDATER", latestUpdaterNightly());
		saveUpdateData();
		updateAvailable[5] = false;
	} else {
		snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading TWiLight Menu++ Updater...\n(Release (3dsx))");
		showProgressBar = true;
		progressBarType = 0;
		createThread((ThreadFunc)displayProgressBar);
		if(downloadFromRelease("https://github.com/RocketRobz/TWiLightMenu-Updater", "TWiLightMenu-Updater\\.3dsx", "/3ds/TWiLightMenu-Updater.3dsx") != 0) {
			downloadFailed();
			return;
		}
		showProgressBar = false;

		setInstalledChannel("TWILIGHTMENU-UPDATER", "release");
		setInstalledVersion("TWILIGHTMENU-UPDATER", latestUpdaterRelease());
		saveUpdateData();
		updateAvailable[4] = false;
	}
	doneMsg();
	if(access("sdmc:/3ds/TWiLightMenu-Updater.3dsx", F_OK) == 0) {
		updated3dsx = true;
	}
}

void updateCheats(void) {
	snprintf(progressBarMsg, sizeof(progressBarMsg), "Downloading DSJ's cheat database...");
	showProgressBar = true;
	progressBarType = 0;
	createThread((ThreadFunc)displayProgressBar);
	if(downloadToFile("https://github.com/TWLBot/Builds/raw/master/usrcheat.dat.7z", "/usrcheat.dat.7z") != 0) {
		downloadFailed();
		return;
	}

	snprintf(progressBarMsg, sizeof(progressBarMsg), "Extracting DSJ's cheat database...");
	filesExtracted = 0;
	progressBarType = 1;
	extractArchive("/usrcheat.dat.7z", "usrcheat.dat", "/_nds/TWiLightMenu/extras/usrcheat.dat");
	showProgressBar = false;

	deleteFile("sdmc:/usrcheat.dat.7z");

	doneMsg();
}

void scanToCancelBoxArt(void) {
	while(continueNdsScan) {
		hidScanInput();
		if(hidKeysDown() & KEY_B) {
			continueNdsScan = false;
		}
		gspWaitForVBlank();
	}
}

int displayMenu(std::string text, std::vector<std::string> menu, std::string options, u32 &hDown) {
    const int max = 10;
	int menuMax = menu.size() - 1;
	int window = max - 1;
	int selectedOption = 0;

	while(1) {
		gspWaitForVBlank();
		hidScanInput();
		hDown = hidKeysDown();
		// Todo: re-add const u32 hHeld = hidKeysHeld();
		
		if((hDown & KEY_A) || (hDown & KEY_X)) {
			return selectedOption;
		} else if(hDown & KEY_B) {
			return -1;
		} else if(hDown & KEY_UP) {
			selectedOption--;
		} else if(hDown & KEY_DOWN) {
			selectedOption++;
		} else if(hDown & KEY_LEFT) {
			selectedOption -= 10;
		} else if(hDown & KEY_RIGHT) {
			selectedOption += 10;
		}
		
		if(selectedOption < 0) {	
			selectedOption = 0;
		}
		if(selectedOption > menuMax) {
			selectedOption = menuMax;
		}

		std::string tmpText = text + "\n";

		if (selectedOption < menuMax && selectedOption + max - 1  < window) {
		    window = selectedOption + max - 1;
		}
		if (selectedOption >= max && selectedOption > window) {
			window = selectedOption;
		}
		if (window < max - 1) {
			window = max - 1;
		}
		int adj = window - max + 1;		
		for (int i = 0; i < max; i++)
		{		     
			int adjIndex = i + adj;

			if (i > menuMax) {
				tmpText += "\n";
				continue;
			}

			if(adjIndex == selectedOption) {
				tmpText += "> " + menu[adjIndex] + "\n";
			} else {
				tmpText += "  " + menu[adjIndex] + "\n";
			}
		}

		tmpText += options;
		displayBottomMsg(tmpText.c_str());
	}
}

u32 displayChoice(std::string text) {
	displayBottomMsg(text.c_str());

	while(1) {
		gspWaitForVBlank();
		hidScanInput();
		const u32 hDown = hidKeysDown();

		if((hDown & KEY_A) || (hDown & KEY_X) || (hDown & KEY_B) || (hDown & KEY_Y)) {
			return hDown;
		}
	}
}

void downloadExtras(void) {
	while (true){
		vector<string> extrasOptions = {"Boxart", "Themes"};
		u32 hDown;
		int selectedOption = displayMenu("What would you like to download?", extrasOptions, "B: Back   A: Choose", hDown);
		if (selectedOption == -1) {
			return;
		}

		if(selectedOption == 0) {
			downloadBoxart();
		} else {
			downloadThemes();
		}
	}
}

void downloadBoxart(void) {
	vector<DirEntry> dirContents;
	std::string scanDir;

	u32 hDown = displayChoice("Would you like to choose a directory, or scan\nthe full card?\n\n\n\n\n\n\n\n\n\nB: Cancel   A: Choose Directory   X: Full SD");

	if(hDown & KEY_A) {
		chdir("sdmc:/");
		bool dirChosen = false;

		while(!dirChosen) {
			vector<string> dirList;
			getDirectoryContents(dirContents);
			for (auto &dir : dirContents) {
				if(dir.isDirectory) {
					dirList.push_back(dir.name);
				}
			}

			int selectedOption = displayMenu("Select a directory..", dirList, "B: Back   A: Open   X: Choose", hDown);

			if (hDown & KEY_A) {
				chdir(dirList[selectedOption].c_str());
			} else if(hDown & KEY_B) {
				char path[PATH_MAX];
				getcwd(path, PATH_MAX);
				if(strcmp(path, "sdmc:/") == 0)	return;
				chdir("..");
			} else if(hDown & KEY_X) {
				chdir(dirList[selectedOption].c_str());
				char path[PATH_MAX];
				getcwd(path, PATH_MAX);
				scanDir = path;
				dirChosen = true;
				break;
			}
		}
	} else if(hDown & KEY_B) {
		return;
	} else if(hDown & KEY_X) {
		scanDir = "sdmc:/";
	}

	vector<string> options = {"Normal (128x115, default)", "Large (210x146)", "Full Screen (256x192)"};
	int boxartSize = displayMenu("Choose a boxart size", options, "B: Back   A: Choose", hDown);
	if (boxartSize == -1) {
		boxartSize = 0;
	}

	options = {"None (Default)", "Line (Black)", "Line (White)", "DSi Theme"};
	int borderStyle = displayMenu("Add a boxart border style?", options, "B: Back   A: Choose", hDown);
	if (borderStyle == -1) {
		borderStyle = 0;
	}

	displayBottomMsg("Scanning SD card for DS roms...\n\n(Press B to cancel)");
	chdir(scanDir.c_str());
	
	continueNdsScan = true;
	createThread((ThreadFunc)scanToCancelBoxArt);
	dirContents.clear(); // KirovAir: This was an annoying fix.. clears previous folder input.
	findCompatibleFiles(dirContents, scanDir.substr(0, scanDir.length()-1));
	continueNdsScan = false;

	mkdir("sdmc:/_nds/TWiLightMenu/boxart", 0777);
	mkdir("sdmc:/_nds/TWiLightMenu/boxart/temp", 0777);
	int overwrite = -1;
	for (auto &romFile : dirContents) {
		char downloadMessage[512];
		snprintf(downloadMessage, sizeof(downloadMessage), "Downloading\n%s.png", romFile.name.c_str());
		displayBottomMsg(downloadMessage);

		if (hasBoxart(romFile.name)) { // Check if exists.
			if (overwrite == -1) {
				snprintf(downloadMessage, sizeof(downloadMessage), "Downloading\n%s.png\n\nBoxart already exists. Overwrite?\n\n\n\n\n\n\n\nA: Yes    B: No    X: Overwrite all    Y: Skip all", romFile.name.c_str());
				hDown = displayChoice(downloadMessage);
				if(hDown & KEY_B) {
					continue;
				} else if(hDown & KEY_X) {
					overwrite = 1;
				} else if(hDown & KEY_Y) {
					overwrite = 0;
				}
			} else if (overwrite == 0) {
				continue;
			}
		}

		// Make a base64 string of first 328 bytes.
		// This will replace titleId with more info to determine DS(i) and gb(c) titles. (And more in the future)
		std::string headerData = getHeaderData(romFile.path.c_str());
								
		// Skip sha1 for DS titles. Header (titleId) and filename are sufficient.
		std::string sha1;
		if (!hasExtension(romFile.name, ".nds") && !hasExtension(romFile.name, ".ds") && !hasExtension(romFile.name, ".dsi")) {
			sha1 = SHA1::from_file(romFile.path);
		}

		char boxartPath[512];
		snprintf(boxartPath, sizeof(boxartPath), "sdmc:/_nds/TWiLightMenu/boxart/temp/%s.png", romFile.name.c_str());
		Result downRes = downloadBoxartToFile(romFile.name, sha1, headerData, boxartPath, boxartSize, borderStyle);

		if (downRes != 0) {
			snprintf(downloadMessage, sizeof(downloadMessage), "Could not download\n%s\n\nA: Continue    B: Cancel", romFile.name.c_str());
			hDown = displayChoice(downloadMessage);

			if (hDown & KEY_A) {
				break;
			} else if(hDown & KEY_B) {
				return;
			}
		}
	}

	chdir("sdmc:/_nds/TWiLightMenu/boxart/temp/");
	getDirectoryContents(dirContents);

	displayBottomMsg("Cleaning up...");
	for (auto &boxart : dirContents) {
		char tempPath[512];
		snprintf(tempPath, sizeof(tempPath), "sdmc:/_nds/TWiLightMenu/boxart/temp/%s", boxart.name.c_str());
		if(boxart.size == 0) {
			deleteFile(tempPath);
		} else {
			char path[512];
			snprintf(path, sizeof(path), "sdmc:/_nds/TWiLightMenu/boxart/%s", boxart.name.c_str());
			deleteFile(path);
			rename(tempPath, path);
		}
	}
	rmdir("sdmc:/_nds/TWiLightMenu/boxart/temp");
	doneMsg();
}

void downloadThemes(void) {
	std::string themeFolders[] = {"dsimenu", "3dsmenu", "r4menu", "akmenu"};

	while (true) {
		vector<string> themeNames = {"DSi theme", "3DS theme", "R4 theme", "Acekard theme"};

		u32 hDown;
		int selectedOption = displayMenu("Which TWiLight theme would you like\nto download themes for?", themeNames, "B: Back   A: Choose", hDown);
		if (selectedOption == -1) {
			return;
		}

		mkdir(("sdmc:/_nds/TWiLightMenu/"+themeFolders[selectedOption]).c_str(), 0777);
		mkdir(("sdmc:/_nds/TWiLightMenu/"+themeFolders[selectedOption]+"/themes/").c_str(), 0777);

		displayBottomMsg("Getting theme list...");
		std::vector<ThemeEntry> themeList = getThemeList("DS-Homebrew/twlmenu-extras", "_nds/TWiLightMenu/"+themeFolders[selectedOption]+"/themes");
		themeNames.clear();		
		for(uint i=0;i<themeList.size();i++) {
			themeNames.push_back(themeList[i].name);
		}

		selectedOption = displayMenu("Select a theme:", themeNames, "B: Back   A: Choose", hDown);
		if(hDown & KEY_A) {
			mkdir((themeList[selectedOption].sdPath).c_str(), 0777);
			displayBottomMsg(("Downloading: "+themeList[selectedOption].name).c_str());
			downloadTheme(themeList[selectedOption].path);
		}
	}
}