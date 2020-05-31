#include "main.h"
#include <fstream>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "audio/bass.h"
#include "iconvlite/iconvlite.h"

using namespace std;

#define FREQ 48000
#define CHANS 1
#define BUFSTEP 200000 // memory allocation unit

int input;			 // current input source
BYTE *recbuf = NULL; // recording buffer
DWORD reclen;		 // recording length
HRECORD rchan = 0;	 // recording channel
HSTREAM chan = 0;	 // playback channel

size_t write_response_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	stringstream *s = (stringstream *)userdata;
	size_t n = size * nmemb;
	s->write(ptr, n);
	return n;
}

size_t read_request_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	ifstream *f = (ifstream *)userdata;
	size_t n = size * nmemb;
	f->read(ptr, n);
	size_t result = f->gcount();
	return result;
}

string recognition(string filename)
{
	CURL *curl = NULL;
	curl = curl_easy_init();

	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_HEADER, 1);
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

		struct curl_slist *headers = NULL;

		headers = curl_slist_append(headers, "Content-Type: audio/x-wav");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		stringstream url;
		url << "http://asr.yandex.net/asr_xml?uuid=12345678123456781234567812345678&disableAntimat=true&topic=general&lang=ru-RU&key=6372dda5-9674-4413-85ff-e9d0eb2f99a7";

		curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

		ifstream fileStream(filename, ifstream::binary);
		fileStream.seekg(0, fileStream.end);
		int length = fileStream.tellg();
		fileStream.seekg(0, fileStream.beg);

		curl_easy_setopt(curl, CURLOPT_READFUNCTION, &read_request_data);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, length);
		curl_easy_setopt(curl, CURLOPT_READDATA, &fileStream);

		stringstream contentStream;

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_response_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &contentStream);

		CURLcode code = curl_easy_perform(curl);

		unsigned httpCode;
		curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &httpCode);
		ofstream myfile;
		myfile.open("SSTT-last-response.log");
		myfile << "Http code is " << httpCode << "\n";
		myfile << contentStream.str() << "\n";
		myfile.close();
		if (httpCode == 200)
		{
			string str = contentStream.str();
			string s2 = "<recognitionResults success=\"0\" />";
			if (strstr(str.c_str(), s2.c_str()))
			{
				curl_free(headers);
				curl_easy_cleanup(curl);
				return "NOT RECOGNIZED";
			}

			size_t pos = str.find("<variant confidence=\"0\">");
			string str3 = str.substr(pos);

			str3 = str3.substr(24);
			str3 = str3.substr(0, str3.find("<"));

			curl_free(headers);
			curl_easy_cleanup(curl);
			return str3;
		}
		else
		{
			curl_free(headers);
			curl_easy_cleanup(curl);
			return "ERROR";
		};
	}
	return 0;
}

// display error messages straight into chat
void Error(const char *es)
{
	char mes[200];
	sprintf(mes, "%s\n(error code: %d)", es, BASS_ErrorGetCode());
	pSAMP->AddChatMessage(-1, mes);
}

BOOL CALLBACK RecordingCallback(HRECORD handle, const void *buffer, DWORD length, void *user)
{
	// increase buffer size if needed
	if ((reclen % BUFSTEP) + length >= BUFSTEP)
	{
		recbuf = (BYTE *)realloc(recbuf, ((reclen + length) / BUFSTEP + 1) * BUFSTEP);
		if (!recbuf)
		{
			rchan = 0;
			Error("Out of memory!");
			return FALSE; // stop recording
		}
	}
	// buffer the data
	memcpy(recbuf + reclen, buffer, length);
	reclen += length;
	return TRUE; // continue recording
}

void StartRecording()
{
	WAVEFORMATEX *wf;
	if (recbuf)
	{ // free old recording
		BASS_StreamFree(chan);
		chan = 0;
		free(recbuf);
		recbuf = NULL;
	}
	// allocate initial buffer and make space for WAVE header
	recbuf = (BYTE *)malloc(BUFSTEP);
	reclen = 44;
	// fill the WAVE header
	memcpy(recbuf, "RIFF\0\0\0\0WAVEfmt \20\0\0\0", 20);
	memcpy(recbuf + 36, "data\0\0\0\0", 8);
	wf = (WAVEFORMATEX *)(recbuf + 20);
	wf->wFormatTag = 1;
	wf->nChannels = CHANS;
	wf->wBitsPerSample = 16;
	wf->nSamplesPerSec = FREQ;
	wf->nBlockAlign = wf->nChannels * wf->wBitsPerSample / 8;
	wf->nAvgBytesPerSec = wf->nSamplesPerSec * wf->nBlockAlign;
	// start recording
	rchan = BASS_RecordStart(FREQ, CHANS, 0, RecordingCallback, 0);
	if (!rchan)
	{
		Error("Can't start recording");
		free(recbuf);
		recbuf = 0;
		return;
	}
}

void StopRecording()
{
	BASS_ChannelStop(rchan);
	rchan = 0;
	// complete the WAVE header
	*(DWORD *)(recbuf + 4) = reclen - 8;
	*(DWORD *)(recbuf + 40) = reclen - 44;
	// create a stream from the recording
	chan = BASS_StreamCreateFile(TRUE, recbuf, 0, reclen, 0);
}
// write the recorded data to disk
void WriteToDisk()
{
	FILE *fp;
	char file[MAX_PATH] = "";
	if (!(fp = fopen("SSTT.wav", "wb")))
	{
		Error("Can't create the file");
		return;
	}

	fwrite(recbuf, reclen, 1, fp);
	fclose(fp);
}

BOOL InitDevice(int device)
{
	BASS_RecordFree(); // free current device (and recording channel) if there is one
	// initalize new device
	if (!BASS_RecordInit(device))
	{
		Error("Can't initialize recording device");
		return FALSE;
	}
	{ // get list of inputs
		int c;
		const char *i;
		input = 0;
		for (c = 0; i = BASS_RecordGetInputName(c); c++)
		{
			if (!(BASS_RecordGetInput(c, NULL) & BASS_INPUT_OFF))
			{ // this one is currently "on"
				input = c;
			}
		}
	}
	return TRUE;
}

void CheckKey(string key)
{
	if (GetKeyState(key[0]) & 0x8000)
	{
		Sleep(600);
		if (!(GetKeyState(key[0]) & 0x8000))
		{
			return;
		}
		pSAMP->AddChatMessage(-1, "[SSTT]: Started Recording");
		StartRecording();
		while (GetKeyState(key[0]) & 0x8000)
		{
			Sleep(100);
			if (reclen > 1024000)
			{
				pSAMP->AddChatMessage(-1, "[SSTT]: �� �������� ������������� ������� �����. ������ ����������.");
				break;
			}
		}
		pSAMP->AddChatMessage(-1, "[SSTT]: Recording Finished");
		StopRecording();
		//pSAMP->AddChatMessage(-1, "[SSTT]: Saving...");
		WriteToDisk();
		//pSAMP->AddChatMessage(-1, "[SSTT]: Saved!");
		//pSAMP->AddChatMessage(-1, "[SSTT]: Recognizion...");
		std::string text = recognition("SSTT.wav");
		if (text == "ERROR")
		{
			pSAMP->AddChatMessage(-1, "[SSTT]: �������������� ������ :(");
			return;
		}
		if (text == "NOT RECOGNIZED")
		{
			pSAMP->AddChatMessage(-1, "[SSTT]: �� ������� ����������!");
			return;
		}

		if (!key.compare("N"))
			text = "/r " + text;
		if (!key.compare("P"))
			text = "/s " + text;
		if (!key.compare("B"))
			text = "/b " + text;
		if (!key.compare("L"))
			text = "/m " + text;
		if (!key.compare("M"))
			text = "/me " + text;

		pSAMP->SendChat(const_cast<char *>(utf2cp(text).c_str()));
		//pSAMP->AddChatMessage(-1, "[SSTT]: Done!");
	}
}

void MainThread()
{
	bool initialized = false;

	while (true)
	{
		if (!initialized)
		{
			if (!pSAMP->IsInitialized())
				continue;

			{
				int c, def;
				BASS_DEVICEINFO di;
				for (c = 0; BASS_RecordGetDeviceInfo(c, &di); c++)
				{
					if (di.flags & BASS_DEVICE_DEFAULT)
					{
						def = c;
					}
				}
				InitDevice(def);
			}

			initialized = true;

			pSAMP->AddChatMessage(-1, "SSTT v31.05.2020 ���������������. ������� �������, ����� ���������. �����: qrlk.me");
			pSAMP->AddChatMessage(-1, "�������: R - ��������, P - ��������, N - �����, M - /me, L - �������, B - /b");
		}

		CheckKey("R");
		CheckKey("P");
		CheckKey("N");
		CheckKey("B");
		CheckKey("M");
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReasonForCall, LPVOID lpReserved)
{
	switch (dwReasonForCall)
	{
	case DLL_PROCESS_ATTACH:
		pSAMP = new SAMP(GetModuleHandleA("SAMP.DLL"));
		beginThread(MainThread);
		break;
	}
	return TRUE;
}