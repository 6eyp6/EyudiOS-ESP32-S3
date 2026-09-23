// ============================================================
// ZipManager.cpp — Eyudio ZIP Extractor (bitbank2/unzipLIB)
// ============================================================
#include "Globals.h"
#include <unzipLIB.h>

// bitbank2/unzip_lib (unzip.h) başarı kodu
#ifndef UNZ_OK
#define UNZ_OK 0
#endif

// SD Kart için dosya sistemi yardımcıları (unzipLIB callback uyumlu)
static void* myOpen(const char *filename, int32_t *size) {
  File f = SD.open(filename);
  if (f) {
    *size = f.size();
    File *fp = new File(f);
    return (void *)fp;
  }
  return NULL;
}

static void myClose(void *p) {
  File *f = (File *)p;
  if (f) {
    f->close();
    delete f;
  }
}

static int32_t myRead(void *p, uint8_t *buffer, int32_t length) {
  File *f = (File *)p;
  if (!f) return 0;
  return f->read(buffer, length);
}

// iType: 0=SET, 1=CUR, 2=END
static int32_t mySeek(void *p, int32_t pos, int type) {
  File *f = (File *)p;
  if (!f) return 0;
  if (type == 0) return f->seek(pos, SeekSet); 
  if (type == 1) return f->seek(pos, SeekCur); 
  if (type == 2) return f->seek(pos, SeekEnd); 
  return 0;
}

bool unzipFile(String zipPath, String destDir, String& errMsg) {
  if (!sdCardPresent) { errMsg = "SD kart yok"; return false; }
  
  if (!zipPath.startsWith("/")) zipPath = "/" + zipPath;
  if (!destDir.startsWith("/")) destDir = "/" + destDir;
  if (!destDir.endsWith("/")) destDir += "/";

  UNZIP unzip;
  // openZIP parametreleri: filename, open, close, read, seek
  int rc = unzip.openZIP(zipPath.c_str(), myOpen, myClose, myRead, mySeek);
  if (rc != UNZ_OK) {
    errMsg = "ZIP acilamadi! (Hata: " + String(rc) + ")";
    return false;
  }

  logToFile("[ZIP] Ayiklama basliyor: " + zipPath);

  char szName[256];
  unz_file_info file_info;
  
  // İlk dosyaya git
  rc = unzip.gotoFirstFile();
  while (rc == UNZ_OK) {
    // Mevcut dosya bilgisini al
    if (unzip.getFileInfo(&file_info, szName, sizeof(szName), NULL, 0, NULL, 0) == UNZ_OK) {
       String fullDest = destDir + String(szName);
       
       // Dizin mi kontrolü (isim '/' ile bitiyorsa)
       if (fullDest.endsWith("/")) {
         SD.mkdir(fullDest);
       } else {
         // Üst dizini oluştur
         int lastSlash = fullDest.lastIndexOf('/');
         if (lastSlash != -1) {
           SD.mkdir(fullDest.substring(0, lastSlash));
         }

         // Dosyayı ayıkla
         if (unzip.openCurrentFile() == UNZ_OK) {
            File out = SD.open(fullDest, FILE_WRITE);
            if (out) {
               uint8_t buffer[1024];
               int readBytes;
               while ((readBytes = unzip.readCurrentFile(buffer, sizeof(buffer))) > 0) {
                  out.write(buffer, readBytes);
               }
               out.close();
            } else {
               logToFile("[ZIP] Dosya olusturulamadi: " + fullDest);
            }
            unzip.closeCurrentFile();
         }
       }
    }
    
    // Sonraki dosyaya geç (-100/UNZ_END_OF_LIST_OF_FILE döndüğünde biter)
    rc = unzip.gotoNextFile();
  }

  unzip.closeZIP();
  logToFile("[ZIP] Ayiklama tamamlandi.");
  return true;
}
