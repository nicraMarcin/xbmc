/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Code below is work in progress and not yet finished
//
// DONE:
// 1) make the code below work
// 2) fetch timeshift buffer filename from TVServerXBMC
// 3) test streaming from timeshift buffer (Skip rtsp, but XBMC's ffmpeg does still the demuxing)
// 4) add Live555 rtsp library (Skip ffmpeg rtsp part, ffmpeg does still the demuxing)
// TODO:
// 5) Move the demuxing part also to this addon to speed up the stream detection (slowest part of ffmpeg); use vdr-vnsi as example
// 6) testing... bugfixing... code cleanup...
// 7) Make code cross platform. Original MediaPortal code is Windows only.

#include "TSReader.h"
#include "client.h" //for XBMC->Log
#include "MultiFileReader.h"
#include "utils.h"
#include "MemoryReader.h"
#include "RTSPClient.h"
#include "platform/util/timeutils.h"

#ifdef TARGET_WINDOWS
#pragma warning(disable:4355)
#endif

using namespace ADDON;

CTsReader::CTsReader():
m_demultiplexer( *this )
{
  m_fileReader      = NULL;
  m_fileDuration    = NULL;
  m_bLiveTv         = false;
  m_bTimeShifting   = false;
  m_bIsRTSP         = false;
  m_cardSettings    = NULL;
  m_State           = State_Stopped;
  m_lastPause       = 0;
  m_WaitForSeekToEof = 0;

#ifdef LIVE555
  m_rtspClient      = NULL;
  m_buffer          = NULL;
#endif
}

CTsReader::~CTsReader(void)
{
  if (m_fileReader)
    delete m_fileReader;
  if (m_fileDuration)
    delete m_fileDuration;
#ifdef LIVE555
  if (m_buffer)
    delete m_buffer;
  if (m_rtspClient)
    delete m_rtspClient;
#endif //LIVE555
}

std::string CTsReader::TranslatePath(const char*  pszFileName)
{
#if defined (TARGET_WINDOWS)
  if (m_basePath.length() == 0)
    return pszFileName;

  CStdString sTimeshiftFile = pszFileName;
  size_t found = string::npos;

  if ((m_cardSettings) && (m_cardSettings->size() > 0))
  {
    for (CCards::iterator it = m_cardSettings->begin(); it < m_cardSettings->end(); it++)
    {
      // Determine whether the first part of the timeshift file name is shared with this card
      found = sTimeshiftFile.find(it->TimeshiftingFolder);
      if (found != string::npos)
      {
        // Remove the original base path and replace it with the given path
        sTimeshiftFile = m_basePath + sTimeshiftFile.substr(it->TimeshiftingFolder.length()+1);
        break;
      }
    }
    XBMC->Log(LOG_INFO, "CTsReader:TranslatePath %s -> %s", pszFileName, sTimeshiftFile.c_str());
    return sTimeshiftFile;
  }
#elif defined(TARGET_LINUX) || defined(TARGET_OSX)
  std::string CIFSname =  pszFileName;
  std::string SMBPrefix = "smb://";
  if (g_szSMBusername.length() > 0)
  {
    SMBPrefix += g_szSMBusername;
    if (g_szSMBpassword.length() > 0)
    {
      SMBPrefix += ":" + g_szSMBpassword;
    }
  }
  else
  {
    SMBPrefix += "Guest";
  }
  SMBPrefix += "@";
  size_t found = string::npos;
  while ((found = CIFSname.find("\\")) != std::string::npos)
  {
    CIFSname.replace(found, 1, "/");
  }
  CIFSname.erase(0,2);
  CIFSname.insert(0, SMBPrefix.c_str());

  XBMC->Log(LOG_INFO, "CTsReader:TranslatePath %s -> %s", pszFileName, CIFSname.c_str());
  return CIFSname;
#endif

  return pszFileName;
}

long CTsReader::Open(const char* pszFileName)
{
  XBMC->Log(LOG_NOTICE, "CTsReader::Open(%s)", pszFileName);

  m_fileName = pszFileName;
  char url[MAX_PATH];
  strncpy(url, m_fileName.c_str(), MAX_PATH);

  // check file type
  int length = strlen(url);

  if ((length > 7) && (strnicmp(url, "rtsp://",7) == 0))
  {
    // rtsp:// stream
    // open stream
    XBMC->Log(LOG_DEBUG, "open rtsp: %s", url);
#ifdef LIVE555
    //strcpy(m_rtspClient.m_outFileName, "e:\\temp\\rtsptest.ts");
    if (m_buffer)
      delete m_buffer;
    if (m_rtspClient)
      delete m_rtspClient;
    m_buffer = new CMemoryBuffer();
    m_rtspClient = new CRTSPClient();
    m_rtspClient->Initialize(m_buffer);

    if ( !m_rtspClient->OpenStream(url))
    {
      SAFE_DELETE(m_rtspClient);
      return E_FAIL;
    }

    m_bIsRTSP = true;
    m_bTimeShifting = true;
    m_bLiveTv = true;

    // are we playing a recording via RTSP
    if (strstr(url, "/stream") == NULL)
    {
      // yes, then we're not timeshifting
      m_bTimeShifting = false;
      m_bLiveTv = false;
    }

    // play
    //m_buffer->Clear();
    //m_buffer->Run(true);
    m_rtspClient->Play(0.0,0.0);
    m_fileReader = new CMemoryReader(*m_buffer);
    m_State = State_Running;
#else
    XBMC->Log(LOG_ERROR, "Failed to open %s. PVR client is compiled without LIVE555 RTSP support.", url);
    XBMC->QueueNotification(QUEUE_ERROR, "PVR client has no RTSP support: %s", url);
    return E_FAIL;
#endif //LIVE555
  }
  else
  {
    if ((length < 9) || (strnicmp(&url[length-9], ".tsbuffer", 9) != 0))
    {
      // local .ts file
      m_bTimeShifting = false;
      m_bLiveTv = false;
      m_bIsRTSP = false;
      m_fileReader = new FileReader();
      m_fileDuration = new FileReader();
    }
    else
    {
      // local timeshift buffer file file
      m_bTimeShifting = true;
      m_bLiveTv = true;
      m_bIsRTSP = false;
      m_fileReader = new MultiFileReader();
      m_fileDuration = new MultiFileReader();
    }

    // Translate path (e.g. Local filepath to smb://user:pass@share)
    m_fileName = TranslatePath(url);

    // open file
    m_fileReader->SetFileName(m_fileName.c_str());
    //m_fileReader->SetDebugOutput(true);
    long retval = m_fileReader->OpenFile();
    if (retval != S_OK)
    {
      XBMC->Log(LOG_ERROR, "Failed to open file '%s' as '%s'", url, m_fileName.c_str());
      return retval;
    }
    //m_fileDuration->SetFileName(m_fileName);
    //m_fileDuration->OpenFile();

    // detect audio/video pids
    m_demultiplexer.SetFileReader(m_fileReader);
    m_demultiplexer.Start();

    //get file duration
    //m_duration.SetFileReader(m_fileDuration);
    //m_duration.UpdateDuration();


    m_fileReader->SetFilePointer(0LL, FILE_BEGIN);
    m_State = State_Running;
  }
  return S_OK;
}

long CTsReader::Read(unsigned char* pbData, unsigned long lDataLength, unsigned long *dwReadBytes)
{
  if (m_fileReader)
  {
    return m_fileReader->Read(pbData, lDataLength, dwReadBytes);
  }

  dwReadBytes = 0;
  return S_FALSE;
}

void CTsReader::Close()
{
  if (m_fileReader)
  {
    if (m_bIsRTSP)
    {
#ifdef LIVE555
      m_rtspClient->Stop();
      SAFE_DELETE(m_rtspClient);
      SAFE_DELETE(m_buffer);
#endif
    }
#ifdef TARGET_WINDOWS
    else
    {
      m_fileReader->CloseFile();
    }
#endif //TARGET_WINDOWS
    SAFE_DELETE(m_fileReader);
    if (m_fileDuration)
      SAFE_DELETE(m_fileDuration);
    m_State = State_Stopped;
  }
}

bool CTsReader::OnZap(const char* pszFileName, int64_t timeShiftBufferPos, long timeshiftBufferID)
{
#ifdef TARGET_WINDOWS
  string newFileName;
  long result;

  XBMC->Log(LOG_NOTICE, "CTsReader::OnZap(%s)", pszFileName);

  // Check whether the new channel url/timeshift buffer is changed
  // In case of a new url/timeshift buffer file, close the old one first
  newFileName = TranslatePath(pszFileName);
  if (newFileName != m_fileName)
  {
    Close();
    result = Open(pszFileName);
    return (result == S_OK);
  }
  else
  {
    if (m_fileReader)
    {
      XBMC->Log(LOG_DEBUG,"OnZap: request new PAT");

      int64_t pos_before, pos_after;
      pos_before = m_fileReader->GetFilePointer();
      result = m_fileReader->SetFilePointer(0LL, FILE_END);
      pos_after = m_fileReader->GetFilePointer();

      if ((timeShiftBufferPos > 0) && (pos_after > timeShiftBufferPos))
      {
        /* Move backward */
        result = m_fileReader->SetFilePointer((timeShiftBufferPos-pos_after), FILE_CURRENT);
        pos_after = m_fileReader->GetFilePointer();
      }
      m_demultiplexer.RequestNewPat();

      XBMC->Log(LOG_DEBUG,"OnZap: move from %I64d to %I64d tsbufpos  %I64d", pos_before, pos_after, timeShiftBufferPos);
      usleep(1000);
      return (result == S_OK);
      //return S_OK;
    }
    return S_FALSE;
  }
#else
  m_fileReader->SetFilePointer(0LL, FILE_END);
  return S_OK;
#endif
}

void CTsReader::SetCardSettings(CCards* cardSettings)
{
  m_cardSettings = cardSettings;
}

void CTsReader::SetDirectory( string& directory )
{
  CStdString tmp = directory;

#ifdef TARGET_WINDOWS
  if( tmp.find("smb://") != string::npos )
  {
    // Convert XBMC smb share name back to a real windows network share...
    tmp.Replace("smb://","\\\\");
    tmp.Replace("/","\\");
  }
#else
  //TODO: do something useful...
#endif
  m_basePath = tmp;
}

bool CTsReader::IsTimeShifting()
{
  return m_bTimeShifting;
}

long CTsReader::Pause()
{
  XBMC->Log(LOG_DEBUG, "CTsReader::Pause() - IsTimeShifting = %d - state = %d", IsTimeShifting(), m_State);

  if (m_State == State_Running)
  {
    m_lastPause = GetTickCount();
#ifdef LIVE555
    // Are we using rtsp?
    if (m_bIsRTSP)
    {
        XBMC->Log(LOG_DEBUG, "CTsReader::Pause()  ->pause rtsp"); // at position: %f", (m_seekTime.Millisecs() / 1000.0f));
        m_rtspClient->Pause();
    }
#endif //LIVE555
    m_State = State_Paused;
  }
  else if (m_State == State_Paused)
  {
#ifdef LIVE555
    // Are we using rtsp?
    if (m_bIsRTSP)
    {
        XBMC->Log(LOG_DEBUG, "CTsReader::Pause() is paused, continue rtsp"); // at position: %f", (m_seekTime.Millisecs() / 1000.0f));
        m_rtspClient->Continue();
        XBMC->Log(LOG_DEBUG, "CTsReader::Pause() rtsp running"); // at position: %f", (m_seekTime.Millisecs() / 1000.0f));
    }
#endif //LIVE555
  }

  XBMC->Log(LOG_DEBUG, "CTsReader::Pause() - END - state = %d", m_State);
  return S_OK;
}

bool CTsReader::IsSeeking()
{
  return (m_WaitForSeekToEof > 0);
}
