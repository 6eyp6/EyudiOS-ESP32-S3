// ============================================================
// BgScriptEngine.cpp — EyuScript Background Task Engine v2
//
// v2 Değişiklikleri (heap fragmentation önleme):
//   - String → fixed char[] buffers (BgContext tamamen POD)
//   - IPC xQueueSend başarısızlığı loglanır (silent data loss yok)
//   - HTTP response boyutu sınırlandırıldı (max 2KB)
//   - BgContext PSRAM'dan allocate edilir (EXT_RAM_ATTR heap)
//
// Mimari:
//   Core 0, Priority 0 — UI (Core 1) ile çakışmaz
//   sdMutex ile SD serialize edilir
//   IPC Queue (16 slot) üzerinden FG'ye veri gönderilir
// ============================================================
#include "Globals.h"
#include <HTTPClient.h>
#include <math.h>

// ── Sabitler ─────────────────────────────────────────────────
#define BG_MAX_LINES      200    // FG'den küçük (400), ama yeterli
#define BG_MAX_LINE_LEN    80    // Satır başına max karakter
#define BG_VAR_NAME_LEN    16    // Değişken adı max
#define BG_VAR_VAL_LEN     48    // Değişken değeri max
#define BG_MAX_LABELS      20
#define BG_HTTP_MAX_BYTES 2048   // HTTP cevabı max 2KB

// ── Fixed-buffer BgContext (POD — heap fragmentation yok) ────
struct BgLabel {
  char name[BG_VAR_NAME_LEN];
  int  lineNumber;
};

struct BgContext {
  int      slotIdx;
  char     varNames [BG_SCRIPT_VARS][BG_VAR_NAME_LEN];
  char     varValues[BG_SCRIPT_VARS][BG_VAR_VAL_LEN];
  int      varCount;
  char     lines    [BG_MAX_LINES][BG_MAX_LINE_LEN];
  int      lineCount;
  BgLabel  labels   [BG_MAX_LABELS];
  int      labelCount;
};

// ── Yardımcı: fixed-buffer string fonksiyonları ───────────────
static void bgSetVar(BgContext& c, const char* name, const char* val) {
  for (int i = 0; i < c.varCount; i++) {
    if (strncmp(c.varNames[i], name, BG_VAR_NAME_LEN) == 0) {
      strncpy(c.varValues[i], val, BG_VAR_VAL_LEN - 1);
      c.varValues[i][BG_VAR_VAL_LEN - 1] = '\0';
      return;
    }
  }
  if (c.varCount < BG_SCRIPT_VARS) {
    strncpy(c.varNames[c.varCount],  name, BG_VAR_NAME_LEN - 1); c.varNames[c.varCount][BG_VAR_NAME_LEN-1] = '\0';
    strncpy(c.varValues[c.varCount], val,  BG_VAR_VAL_LEN  - 1); c.varValues[c.varCount][BG_VAR_VAL_LEN-1] = '\0';
    c.varCount++;
  }
}
static const char* bgGetVar(BgContext& c, const char* name) {
  for (int i = 0; i < c.varCount; i++)
    if (strncmp(c.varNames[i], name, BG_VAR_NAME_LEN) == 0)
      return c.varValues[i];
  return "";
}

// {var} → değer expand (1 geçiş, statik buffer)
static const char* bgExpand(BgContext& c, const char* src, char* dst, int dstLen) {
  int di = 0;
  for (int i = 0; src[i] && di < dstLen - 1; ) {
    if (src[i] == '{') {
      int j = i + 1;
      while (src[j] && src[j] != '}') j++;
      char vname[BG_VAR_NAME_LEN] = {};
      int vlen = j - i - 1;
      if (vlen > 0 && vlen < BG_VAR_NAME_LEN) {
        strncpy(vname, src + i + 1, vlen);
        const char* vv = bgGetVar(c, vname);
        int vvl = strlen(vv);
        if (di + vvl < dstLen - 1) { strncpy(dst + di, vv, vvl); di += vvl; }
      }
      i = (src[j] == '}') ? j + 1 : j;
    } else {
      dst[di++] = src[i++];
    }
  }
  dst[di] = '\0';
  return dst;
}

// Expand ya da direkt değer (literal vs {var})
static void bgEval(BgContext& c, const char* expr, char* out, int outLen) {
  if (expr[0] == '{' && expr[strlen(expr)-1] == '}') {
    char vname[BG_VAR_NAME_LEN] = {};
    int l = strlen(expr) - 2;
    if (l > 0 && l < BG_VAR_NAME_LEN) strncpy(vname, expr + 1, l);
    strncpy(out, bgGetVar(c, vname), outLen - 1); out[outLen-1] = '\0';
  } else {
    bgExpand(c, expr, out, outLen);
  }
}

static bool bgCompare(BgContext& c, const char* left, const char* op, const char* right) {
  char lv[BG_VAR_VAL_LEN], rv[BG_VAR_VAL_LEN];
  bgEval(c, left, lv, sizeof(lv));
  bgEval(c, right, rv, sizeof(rv));
  float l = atof(lv), r = atof(rv);
  if (!strcmp(op,"==")) return l == r;
  if (!strcmp(op,"!=")) return l != r;
  if (!strcmp(op,">"))  return l >  r;
  if (!strcmp(op,"<"))  return l <  r;
  if (!strcmp(op,">=")) return l >= r;
  if (!strcmp(op,"<=")) return l <= r;
  return false;
}

// ── IPC: FG'ye gönder (drop loglu) ──────────────────────────
static void bgIpc(int slot, const char* key, const char* val) {
  if (!ipcQueue) return;
  IpcMsg msg;
  strncpy(msg.key, key, sizeof(msg.key)-1); msg.key[sizeof(msg.key)-1] = '\0';
  strncpy(msg.val, val, sizeof(msg.val)-1); msg.val[sizeof(msg.val)-1] = '\0';
  if (xQueueSend(ipcQueue, &msg, 0) != pdTRUE) {
    // Silent data loss önleme: Serial'e yaz
    Serial.printf("[BG%d] IPC KUYRUGU DOLU — mesaj dustu: %s=%s\n", slot, key, val);
  }
}

// ── Label ara ────────────────────────────────────────────────
static int bgFindLabel(BgContext& c, const char* name) {
  for (int i = 0; i < c.labelCount; i++)
    if (strncmp(c.labels[i].name, name, BG_VAR_NAME_LEN) == 0)
      return c.labels[i].lineNumber;
  return -1;
}

// ── Satırı cmd + params'a böl (in-place, statik) ─────────────
static void bgParse(const char* line, char* cmd, int cmdLen, char* params, int paramsLen) {
  const char* sp = strchr(line, ' ');
  if (!sp) {
    strncpy(cmd, line, cmdLen - 1); cmd[cmdLen-1] = '\0';
    params[0] = '\0';
  } else {
    int cLen = sp - line;
    if (cLen >= cmdLen) cLen = cmdLen - 1;
    strncpy(cmd, line, cLen); cmd[cLen] = '\0';
    // Küçük harf
    for (int i = 0; cmd[i]; i++) cmd[i] = tolower((uint8_t)cmd[i]);
    strncpy(params, sp + 1, paramsLen - 1); params[paramsLen-1] = '\0';
    // Baştaki boşlukları kırp
    while (*params == ' ') memmove(params, params+1, strlen(params));
  }
  for (int i = 0; cmd[i]; i++) cmd[i] = tolower((uint8_t)cmd[i]);
}

// ── Operatörü bul (>= <= önce, > < sonra) ────────────────────
static const char* findBgOp(const char* cond, int& opStart) {
  static const char* OPS[] = {"==","!=",">=","<=",">","<",nullptr};
  opStart = -1;
  for (int o = 0; OPS[o]; o++) {
    const char* p = strstr(cond, OPS[o]);
    if (p) { opStart = p - cond; return OPS[o]; }
  }
  return nullptr;
}

// ── Koşul değerlendir ────────────────────────────────────────
static bool bgEvalCond(BgContext& c, const char* cond) {
  int opStart;
  const char* op = findBgOp(cond, opStart);
  if (!op) {
      char tmp[BG_VAR_VAL_LEN];
      bgEval(c, cond, tmp, sizeof(tmp));
      return atof(tmp) != 0;
  }
  char left[BG_VAR_VAL_LEN] = {}, right[BG_VAR_VAL_LEN] = {};
  int opLen = strlen(op);
  strncpy(left, cond, min(opStart, (int)sizeof(left)-1));
  // Trim left
  char* l = left; while (*l == ' ') l++; int ll = strlen(l); while (ll > 0 && l[ll-1]==' ') l[--ll]='\0';
  const char* rp = cond + opStart + opLen;
  while (*rp == ' ') rp++;
  strncpy(right, rp, sizeof(right)-1);
  int rl = strlen(right); while (rl > 0 && right[rl-1]==' ') right[--rl]='\0';
  return bgCompare(c, l, op, right);
}

// ── Ana BG task ───────────────────────────────────────────────
static void bgScriptTask(void* arg) {
  int slotIdx = (int)(intptr_t)arg;
  BgTask& bt  = bgTasks[slotIdx];

  // BgContext: PSRAM'dan al (ps_malloc), yoksa normal heap
  BgContext* ctx = (BgContext*)ps_malloc(sizeof(BgContext));
  if (!ctx) ctx = (BgContext*)malloc(sizeof(BgContext));
  if (!ctx) {
    Serial.printf("[BG%d] HATA: BgContext icin bellek yok!\n", slotIdx);
    bt.active = false; bt.handle = NULL;
    vTaskDelete(NULL); return;
  }
  memset(ctx, 0, sizeof(BgContext));
  ctx->slotIdx = slotIdx;

  // ── Pass 1: sdMutex ile dosya oku ────────────────────────
  if (!sdMutex || xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    Serial.printf("[BG%d] HATA: sdMutex timeout!\n", slotIdx);
    free(ctx); bt.active = false; bt.handle = NULL;
    vTaskDelete(NULL); return;
  }

  File f = SD.open(bt.filePath.c_str());
  if (f) {
    while (f.available() && ctx->lineCount < BG_MAX_LINES) {
      char buf[BG_MAX_LINE_LEN];
      int i = 0;
      while (f.available() && i < BG_MAX_LINE_LEN - 1) {
        char ch = f.read();
        if (ch == '\n') break;
        if (ch != '\r') buf[i++] = ch;
      }
      buf[i] = '\0';
      // Trim
      int start = 0; while (buf[start] == ' ' || buf[start] == '\t') start++;
      if (start > 0) memmove(buf, buf+start, strlen(buf+start)+1);
      int len = strlen(buf); while (len > 0 && (buf[len-1]==' '||buf[len-1]=='\t')) buf[--len]='\0';

      if (len == 0) continue;
      // Yorum satırı atla
      if (buf[0] == '#' || (buf[0] == '/' && buf[1] == '/')) continue;

      strncpy(ctx->lines[ctx->lineCount], buf, BG_MAX_LINE_LEN-1);
      ctx->lines[ctx->lineCount][BG_MAX_LINE_LEN-1] = '\0';

      // Label pre-scan
      if (buf[0] == ':' && ctx->labelCount < BG_MAX_LABELS) {
        strncpy(ctx->labels[ctx->labelCount].name, buf+1, BG_VAR_NAME_LEN-1);
        ctx->labels[ctx->labelCount].name[BG_VAR_NAME_LEN-1] = '\0';
        // Trim label name
        char* ln = ctx->labels[ctx->labelCount].name;
        int ll = strlen(ln); while (ll>0 && (ln[ll-1]==' '||ln[ll-1]=='\t')) ln[--ll]='\0';
        ctx->labels[ctx->labelCount].lineNumber = ctx->lineCount;
        ctx->labelCount++;
      }
      ctx->lineCount++;
    }
    f.close();
    Serial.printf("[BG%d] Pass1: %d satir, %d label\n", slotIdx, ctx->lineCount, ctx->labelCount);
  } else {
    Serial.printf("[BG%d] HATA: Dosya acilamadi: %s\n", slotIdx, bt.filePath.c_str());
    xSemaphoreGive(sdMutex);
    free(ctx); bt.active = false; bt.handle = NULL;
    vTaskDelete(NULL); return;
  }
  xSemaphoreGive(sdMutex);  // SD serbest — okuma bitti

  // ── Local stack'ler (küçük, stack'te güvenli) ────────────
  int gosubStk[MAX_GOSUB_DEPTH]; int gosubTp = -1;
  typedef struct { int startLine; char cvar[BG_VAR_NAME_LEN]; int cend; int cstep; } BgLoop;
  BgLoop lpStk[MAX_LOOP_DEPTH]; int lpTop = -1;

  int pc = 0;
  char cmd[24], params[BG_MAX_LINE_LEN];
  char tmp1[BG_VAR_VAL_LEN], tmp2[BG_VAR_VAL_LEN];

  while (pc < ctx->lineCount) {
    vTaskDelay(1);  // Cooperative yield — her iterasyonda scheduler'a nefes ver

    bgParse(ctx->lines[pc], cmd, sizeof(cmd), params, sizeof(params));

    if (cmd[0] == ':') { pc++; continue; }   // label marker

    // ── SET ────────────────────────────────────────────────
    if (!strcmp(cmd, "set")) {
      char* eq = strchr(params, '=');
      if (eq) {
        *eq = '\0';
        char vname[BG_VAR_NAME_LEN] = {};
        strncpy(vname, params, sizeof(vname)-1);
        int l = strlen(vname); while (l>0&&(vname[l-1]==' '||vname[l-1]=='\t')) vname[--l]='\0';
        char* rhs = eq + 1; while (*rhs == ' ') rhs++;
        bgEval(*ctx, rhs, tmp1, sizeof(tmp1));
        bgSetVar(*ctx, vname, tmp1);
      }

    // ── ARİTMETİK ──────────────────────────────────────────
    } else if (!strcmp(cmd,"add")||!strcmp(cmd,"sub")||!strcmp(cmd,"mul")||
               !strcmp(cmd,"div")||!strcmp(cmd,"mod")) {
      char vn[BG_VAR_NAME_LEN]={}, aStr[BG_VAR_VAL_LEN]={}, bStr[BG_VAR_VAL_LEN]={};
      char* p1=params, *p2=strchr(p1,' '), *p3=(p2?strchr(p2+1,' '):nullptr);
      if (p2 && p3) {
        int vnL=p2-p1; if(vnL>=BG_VAR_NAME_LEN)vnL=BG_VAR_NAME_LEN-1;
        strncpy(vn,p1,vnL); vn[vnL]='\0';
        int aL=p3-p2-1; if(aL>=(int)sizeof(aStr))aL=(int)sizeof(aStr)-1;
        strncpy(aStr,p2+1,aL); aStr[aL]='\0';
        strncpy(bStr,p3+1,sizeof(bStr)-1);
        bgEval(*ctx,aStr,tmp1,sizeof(tmp1)); bgEval(*ctx,bStr,tmp2,sizeof(tmp2));
        double a=atof(tmp1), b=atof(tmp2), res=0;
        if      (!strcmp(cmd,"add")) res=a+b;
        else if (!strcmp(cmd,"sub")) res=a-b;
        else if (!strcmp(cmd,"mul")) res=a*b;
        else if (!strcmp(cmd,"div")) res=(b!=0)?a/b:0;
        else if (!strcmp(cmd,"mod")) res=fmod(a,b);
        // Tam sayı ise nokta koyma
        char resBuf[BG_VAR_VAL_LEN];
        if (res == (long long)res) snprintf(resBuf,sizeof(resBuf),"%lld",(long long)res);
        else                        snprintf(resBuf,sizeof(resBuf),"%.4f",res);
        bgSetVar(*ctx, vn, resBuf);
      }
    } else if (!strcmp(cmd,"inc")) {
      long v=atol(bgGetVar(*ctx,params)); char buf[16]; snprintf(buf,sizeof(buf),"%ld",v+1);
      bgSetVar(*ctx,params,buf);
    } else if (!strcmp(cmd,"dec")) {
      long v=atol(bgGetVar(*ctx,params)); char buf[16]; snprintf(buf,sizeof(buf),"%ld",v-1);
      bgSetVar(*ctx,params,buf);
    } else if (!strcmp(cmd, "random")) {
      char* sp = strchr(params, ' '); if (sp) {
        *sp = '\0'; int minv = atoi(params); char* sp2 = strchr(sp+1, ' ');
        if (sp2) { *sp2 = '\0'; int maxv = atoi(sp+1); bgSetVar(*ctx, sp2+1, String(random(minv, maxv+1)).c_str()); }
        *sp = ' ';
      }
    } else if (!strcmp(cmd, "sqrt") || !strcmp(cmd, "sin") || !strcmp(cmd, "cos") || !strcmp(cmd, "tan") || !strcmp(cmd, "abs")) {
       bgEval(*ctx, params, tmp1, sizeof(tmp1)); double v = atof(tmp1), res = 0;
       if (!strcmp(cmd, "sqrt")) res = sqrt(v); else if (!strcmp(cmd, "sin")) res = sin(v);
       else if (!strcmp(cmd, "cos")) res = cos(v); else if (!strcmp(cmd, "tan")) res = tan(v);
       else if (!strcmp(cmd, "abs")) res = fabs(v);
       char rBuf[16]; snprintf(rBuf, sizeof(rBuf), "%.4f", res); bgSetVar(*ctx, "math_result", rBuf);
    } else if (!strcmp(cmd, "pow")) {
       char* sp = strchr(params, ' '); if (sp) {
         *sp = '\0'; bgEval(*ctx, params, tmp1, sizeof(tmp1)); bgEval(*ctx, sp+1, tmp2, sizeof(tmp2));
         char rBuf[16]; snprintf(rBuf, sizeof(rBuf), "%.4f", pow(atof(tmp1), atof(tmp2)));
         bgSetVar(*ctx, "math_result", rBuf); *sp = ' ';
       }

    // ── IPC (drop loglu) ───────────────────────────────────
    } else if (!strcmp(cmd,"ipc")) {
      char* sp = strchr(params,' ');
      if (sp) {
        char key[BG_VAR_NAME_LEN]={};
        int kl=sp-params; if(kl>=BG_VAR_NAME_LEN)kl=BG_VAR_NAME_LEN-1;
        strncpy(key,params,kl);
        bgEval(*ctx,sp+1,tmp1,sizeof(tmp1));
        bgIpc(slotIdx, key, tmp1);   // drop loglu versiyon
      }
    } else if (!strcmp(cmd, "strlen")) {
      bgEval(*ctx, params, tmp1, sizeof(tmp1)); char rb[8]; snprintf(rb, sizeof(rb), "%d", strlen(tmp1));
      bgSetVar(*ctx, "strlen_result", rb);
    } else if (!strcmp(cmd, "toupper") || !strcmp(cmd, "tolower")) {
      bgEval(*ctx, params, tmp1, sizeof(tmp1));
      for (int i=0; tmp1[i]; i++) tmp1[i] = (!strcmp(cmd, "toupper")) ? toupper(tmp1[i]) : tolower(tmp1[i]);
      bgSetVar(*ctx, params, tmp1);

    // ── LOG ────────────────────────────────────────────────
    } else if (!strcmp(cmd,"log")) {
      char expanded[BG_MAX_LINE_LEN];
      bgExpand(*ctx, params, expanded, sizeof(expanded));
      Serial.printf("[BG%d] %s\n", slotIdx, expanded);

    // ── WAIT (cooperative) ─────────────────────────────────
    } else if (!strcmp(cmd,"wait")) {
      bgEval(*ctx,params,tmp1,sizeof(tmp1));
      int ms = atoi(tmp1);
      unsigned long t0 = millis();
      while ((int)(millis()-t0) < ms) vTaskDelay(5);

    } else if (!strcmp(cmd,"yield")) {
      vTaskDelay(10);

    // ── ZAMAN ──────────────────────────────────────────────
    } else if (!strcmp(cmd,"gettime")) {
      uint32_t s=millis()/1000; char buf[12];
      snprintf(buf,sizeof(buf),"%02d:%02d:%02d",s/3600,(s%3600)/60,s%60);
      bgSetVar(*ctx,"gettime_result",buf);
    } else if (!strcmp(cmd,"timestamp")) {
      char buf[12]; snprintf(buf,sizeof(buf),"%lu",millis());
      bgSetVar(*ctx,"timestamp_result",buf);
    } else if (!strcmp(cmd,"uptime")) {
      char buf[12]; snprintf(buf,sizeof(buf),"%lu",millis()/1000);
      bgSetVar(*ctx,"uptime_result",buf);

    // ── GPIO ───────────────────────────────────────────────
    } else if (!strcmp(cmd,"digitalread")) {
      char buf[4]; snprintf(buf,sizeof(buf),"%d",digitalRead(atoi(params)));
      bgSetVar(*ctx,"lastread",buf);
    } else if (!strcmp(cmd,"analogread")) {
      char buf[8]; snprintf(buf,sizeof(buf),"%d",analogRead(atoi(params)));
      bgSetVar(*ctx,"lastread",buf);
    } else if (!strcmp(cmd,"digitalwrite")) {
      char* c2=strchr(params,',');
      if(c2) { char tmp[8]; strncpy(tmp,c2+1,7); tmp[7]='\0';
        while(*tmp==' ')memmove(tmp,tmp+1,strlen(tmp)); // ltrim
        int pin=atoi(params); int val=(!strcasecmp(tmp,"HIGH")||atoi(tmp)?HIGH:LOW);
        digitalWrite(pin,val); }
    } else if (!strcmp(cmd,"pinmode")) {
      char* c2=strchr(params,',');
      if(c2) { char m[16]; strncpy(m,c2+1,15); m[15]='\0';
        while(*m==' ')memmove(m,m+1,strlen(m));
        int pin=atoi(params);
        if(!strcasecmp(m,"OUTPUT"))      pinMode(pin,OUTPUT);
        else if(!strcasecmp(m,"INPUT_PULLUP")) pinMode(pin,INPUT_PULLUP);
        else                             pinMode(pin,INPUT); }

    // ── DOSYA (sdMutex korumalı) ───────────────────────────
    } else if (!strcmp(cmd,"fileread")) {
      if (sdMutex && xSemaphoreTake(sdMutex,pdMS_TO_TICKS(1000))==pdTRUE) {
        File rf=SD.open(params);
        if(rf) {
          char rbuf[BG_VAR_VAL_LEN]={};
          int ri=0;
          while(rf.available()&&ri<(int)sizeof(rbuf)-1) rbuf[ri++]=rf.read();
          rbuf[ri]='\0'; rf.close();
          bgSetVar(*ctx,"fileread_result",rbuf);
        }
        xSemaphoreGive(sdMutex);
      }
    } else if (!strcmp(cmd,"filewrite")) {
      char* sp=strchr(params,' ');
      if(sp&&sdMutex&&xSemaphoreTake(sdMutex,pdMS_TO_TICKS(1000))==pdTRUE) {
        *sp='\0'; File wf=SD.open(params,FILE_WRITE);
        if(wf){bgEval(*ctx,sp+1,tmp1,sizeof(tmp1)); wf.print(tmp1); wf.close();}
        *sp=' '; xSemaphoreGive(sdMutex);
      }
    } else if (!strcmp(cmd,"fileappend")) {
      char* sp=strchr(params,' ');
      if(sp&&sdMutex&&xSemaphoreTake(sdMutex,pdMS_TO_TICKS(1000))==pdTRUE) {
        *sp='\0'; File af=SD.open(params,FILE_APPEND);
        if(af){bgEval(*ctx,sp+1,tmp1,sizeof(tmp1)); af.print(tmp1); af.close();}
        *sp=' '; xSemaphoreGive(sdMutex);
      }

    // ── WiFi ───────────────────────────────────────────────
    } else if (!strcmp(cmd,"wifistatus")) {
      int st=WiFi.status();
      bgSetVar(*ctx,"wifistatus", st==WL_CONNECTED?"connected":st==WL_CONNECT_FAILED?"failed":"disconnected");
    } else if (!strcmp(cmd,"getip")) {
      bgSetVar(*ctx,"getip_result", WiFi.status()==WL_CONNECTED?WiFi.localIP().toString().c_str():"no_ip");

    // ── HTTP GET (boyut limiti: BG_HTTP_MAX_BYTES = 2KB) ──
    } else if (!strcmp(cmd,"httpget")) {
      HTTPClient http;
      bgEval(*ctx, params, tmp1, sizeof(tmp1));
      http.begin(tmp1);
      http.setTimeout(5000);   // 5s timeout
      int code = http.GET();
      char codeBuf[8]; snprintf(codeBuf,sizeof(codeBuf),"%d",code);
      bgSetVar(*ctx,"httpget_result",codeBuf);
      if (code == 200) {
        int len = http.getSize();
        WiFiClient* stream = http.getStreamPtr();
        char httpBuf[BG_HTTP_MAX_BYTES + 1] = {};
        int read = 0;
        while (stream->available() && read < BG_HTTP_MAX_BYTES) {
          httpBuf[read++] = stream->read();
        }
        httpBuf[read] = '\0';
        if (read >= BG_HTTP_MAX_BYTES)
          Serial.printf("[BG%d] HTTP yanit kirpildi (%d B limit)\n", slotIdx, BG_HTTP_MAX_BYTES);
        bgSetVar(*ctx,"httpresponse",httpBuf);
      }
      http.end();

    // ── GOTO ───────────────────────────────────────────────
    } else if (!strcmp(cmd,"goto")) {
      // Trim label name
      char lbl[BG_VAR_NAME_LEN] = {};
      strncpy(lbl, params, sizeof(lbl)-1);
      int l=strlen(lbl); while(l>0&&(lbl[l-1]==' '||lbl[l-1]=='\t'))lbl[--l]='\0';
      int t = bgFindLabel(*ctx, lbl);
      if (t != -1) { pc = t; continue; }
      else Serial.printf("[BG%d] HATA: Label bulunamadi: %s\n", slotIdx, lbl);

    // ── GOSUB / RETURN ─────────────────────────────────────
    } else if (!strcmp(cmd,"gosub")) {
      if (gosubTp < MAX_GOSUB_DEPTH - 1) {
        gosubStk[++gosubTp] = pc + 1;
        int t = bgFindLabel(*ctx, params);
        if (t != -1) { pc = t; continue; }
        else { gosubTp--; Serial.printf("[BG%d] GOSUB label yok: %s\n", slotIdx, params); }
      } else Serial.printf("[BG%d] GOSUB stack dolu!\n", slotIdx);
    } else if (!strcmp(cmd,"return")) {
      if (gosubTp >= 0) { pc = gosubStk[gosubTp--]; continue; }
      else Serial.printf("[BG%d] RETURN ama stack bos!\n", slotIdx);

    // ── IF / ELSE / ENDIF ──────────────────────────────────
    } else if (!strcmp(cmd,"if")) {
      // "then" varsa ayır
      char cond[BG_MAX_LINE_LEN] = {};
      char* thenp = strstr(params, " then ");
      if (thenp) { int cl=thenp-params; if(cl>=(int)sizeof(cond))cl=(int)sizeof(cond)-1; strncpy(cond,params,cl); }
      else strncpy(cond, params, sizeof(cond)-1);

      bool res = bgEvalCond(*ctx, cond);

      if (res && thenp) {
        // Tek satır IF: inline komut
        char inlineCmd[BG_MAX_LINE_LEN]; strncpy(inlineCmd, thenp+6, sizeof(inlineCmd)-1);
        // Satırı ekle ve geri al tekniği yerine basit goto
        if (!strncmp(inlineCmd,"goto ",5)) {
          char lbl[BG_VAR_NAME_LEN]={}; strncpy(lbl,inlineCmd+5,sizeof(lbl)-1);
          int tgt=bgFindLabel(*ctx,lbl);
          if(tgt!=-1){pc=tgt;continue;}
        }
        // Diğer inline komutlar için basit parse — "end/stop"
        else if (!strncmp(inlineCmd,"end",3)||!strncmp(inlineCmd,"stop",4)) { goto bg_done; }
      } else if (!res) {
        // Koşul yanlış → ELSE veya ENDIF'e atla
        int depth=1; pc++;
        while (pc < ctx->lineCount && depth > 0) {
          char sc[24],sp2[BG_MAX_LINE_LEN];
          bgParse(ctx->lines[pc],sc,sizeof(sc),sp2,sizeof(sp2));
          if (!strcmp(sc,"if")) depth++;
          else if (!strcmp(sc,"endif")) depth--;
          else if (!strcmp(sc,"else") && depth==1) break;
          if (depth > 0) pc++;
        }
        continue;
      }
    } else if (!strcmp(cmd,"else")) {
      int depth=1; pc++;
      while (pc<ctx->lineCount&&depth>0) {
        char sc[24],sp2[BG_MAX_LINE_LEN];
        bgParse(ctx->lines[pc],sc,sizeof(sc),sp2,sizeof(sp2));
        if (!strcmp(sc,"if")) depth++;
        else if (!strcmp(sc,"endif")) depth--;
        if (depth>0) pc++;
      }
      continue;
    } else if (!strcmp(cmd,"endif")) {
      // marker

    // ── FOR / NEXT ─────────────────────────────────────────
    } else if (!strcmp(cmd,"for")) {
      char* eq=strchr(params,'='), *to=strstr(params," to "), *st=strstr(params," step ");
      if (eq && to && lpTop < MAX_LOOP_DEPTH-1) {
        char vn[BG_VAR_NAME_LEN]={};
        int vnl=eq-params; if(vnl>=BG_VAR_NAME_LEN)vnl=BG_VAR_NAME_LEN-1;
        strncpy(vn,params,vnl);
        bgEval(*ctx,eq+1,tmp1,sizeof(tmp1));  // start yok sadece end
        // Başlangıç değeri: = ile to arasındaki kısım
        char startS[BG_VAR_VAL_LEN]={};
        int sl=to-eq-1; if(sl>=BG_VAR_VAL_LEN)sl=BG_VAR_VAL_LEN-1;
        strncpy(startS,eq+1,sl);
        bgEval(*ctx,startS,tmp1,sizeof(tmp1));
        int sv=atoi(tmp1);
        // end
        char endS[BG_VAR_VAL_LEN]={};
        const char* endPtr=to+4;
        int el=st?(st-endPtr):(int)strlen(endPtr); if(el>=BG_VAR_VAL_LEN)el=BG_VAR_VAL_LEN-1;
        strncpy(endS,endPtr,el); bgEval(*ctx,endS,tmp1,sizeof(tmp1)); int ev=atoi(tmp1);
        int step=1;
        if(st){bgEval(*ctx,st+6,tmp1,sizeof(tmp1)); step=atoi(tmp1); if(step==0)step=1;}
        lpStk[++lpTop]={pc,{},ev,step};
        strncpy(lpStk[lpTop].cvar,vn,BG_VAR_NAME_LEN-1);
        char sv2[16]; snprintf(sv2,sizeof(sv2),"%d",sv); bgSetVar(*ctx,vn,sv2);
      }
    } else if (!strcmp(cmd,"next")) {
      if (lpTop>=0) {
        BgLoop& lp=lpStk[lpTop];
        int cur=atoi(bgGetVar(*ctx,lp.cvar))+lp.cstep;
        char cv[16]; snprintf(cv,sizeof(cv),"%d",cur); bgSetVar(*ctx,lp.cvar,cv);
        bool cont=(lp.cstep>0)?(cur<=lp.cend):(cur>=lp.cend);
        if(cont){pc=lp.startLine;continue;} else lpTop--;
      }

    // ── WHILE / WEND ───────────────────────────────────────
    } else if (!strcmp(cmd,"while")) {
      if (bgEvalCond(*ctx, params)) {
        if (lpTop < MAX_LOOP_DEPTH-1) lpStk[++lpTop]={pc,{},0,0};
      } else {
        int depth=1; pc++;
        while (pc<ctx->lineCount&&depth>0) {
          char sc[24],sp2[BG_MAX_LINE_LEN];
          bgParse(ctx->lines[pc],sc,sizeof(sc),sp2,sizeof(sp2));
          if (!strcmp(sc,"while")) depth++;
          else if (!strcmp(sc,"wend")) depth--;
          if (depth>0) pc++;
        }
        continue;
      }
    } else if (!strcmp(cmd,"wend")) {
      if (lpTop>=0) { pc=lpStk[lpTop].startLine; lpTop--; continue; }

    // ── END / STOP ─────────────────────────────────────────
    } else if (!strcmp(cmd,"end")||!strcmp(cmd,"stop")) {
      break;
    }

    pc++;
  }

bg_done:
  free(ctx);   // PSRAM ya da normal heap — her iki durumda da geçerli
  Serial.printf("[BG%d] Tamamlandi. Heap: %u B\n", slotIdx, ESP.getFreeHeap());
  bgTasks[slotIdx].active = false;
  bgTasks[slotIdx].handle = NULL;
  vTaskDelete(NULL);
}

// ── Public API ───────────────────────────────────────────────
int runBgScript(const String& filePath) {
  int slot = -1;
  for (int i = 0; i < MAX_BG_TASKS; i++)
    if (!bgTasks[i].active) { slot = i; break; }
  if (slot == -1) return -1;

  bgTasks[slot].active     = true;
  bgTasks[slot].filePath   = filePath;
  bgTasks[slot].handle     = NULL;
  bgTasks[slot].msgPending = false;

  char taskName[16];
  snprintf(taskName, sizeof(taskName), "BGScript%d", slot);

  // Core 0 — UI Core 1'de, WiFi/BT Core 0'da ama Priority 0 onları bloke etmez
  xTaskCreatePinnedToCore(
    bgScriptTask, taskName,
    7168,                       // 7KB stack (BgContext heap'te, stack küçük kalır)
    (void*)(intptr_t)slot,
    0,                          // Priority 0 = en düşük → UI ve WiFi önce çalışır
    &bgTasks[slot].handle,
    0                           // Core 0
  );
  return slot;
}

void stopBgScript(int slot) {
  if (slot < 0 || slot >= MAX_BG_TASKS || !bgTasks[slot].active) return;
  if (bgTasks[slot].handle) {
    vTaskDelete(bgTasks[slot].handle);
    bgTasks[slot].handle = NULL;
  }
  bgTasks[slot].active = false;
  Serial.printf("[BG%d] Zorla durduruldu.\n", slot);
}
