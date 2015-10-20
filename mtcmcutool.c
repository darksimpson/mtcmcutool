#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Arhument parser helper sugar */
#define _ARGHLP(a, s) (!strncmp(a, s, strlen(s)))
/* Error handling helper sugar */
#define _ERRHLP_MRL(m, r, l) { printf(m); result = r; goto l; }
#define _ERRHLP_ML(m, l) { printf(m); goto l; }
#define _ERRHLP_RL(r, l) { result = r; goto l; }

void print_banner()
{
  printf("mtcmcutool: a tool to manipulate MTC MCU firmware images for RK3066/RK3188 headunits.\n");
  printf("            Version: 1.1\n");
  printf("            Copyright (c) 2015, Dark Simpson\n");
  printf("\n");
}

void print_usage()
{
  printf("usage: mtcmcutool -d <encoded file name> <decoded file name>\n");
  printf("         to decode MTC MCU image to plain binary, where:\n");
  printf("           <encoded file name> is original encoded MCU file name from\n");
  printf("                               firmware package (usually simply \"mcu.img\").\n");
  printf("           <decoded file name> is file name where to save decoded MCU\n");
  printf("                               image (you can use \"mcu.bin\" for example).\n");
  printf("\n");
  printf("       mtcmcutool -e <decoded file name> <encoded file name>\n");
  printf("         to encode plain binary MCU image to MTC format, where:\n");
  printf("           <decoded file name> is plain binary MCU image file name to\n");
  printf("                               encode(\"mcu.bin\" for example).\n");
  printf("           <encoded file name> is resultimg encoded MCU image file\n");
  printf("                               name in MTC format (usually \"mcu.img\").\n");
  printf("\n");
  printf("       mtcmcutool -v <file name>\n");
  printf("         to try to heuristically determine and print MCU version, where:\n");
  printf("           <file name> is encoded or plain MCU image file name that you\n");
  printf("                       want to try to extract version information from.\n");
}

/* That's all the magic of MCU firmware "encryption" ;) */
void xor_cnt(uint8_t *buf, int len, uint8_t cnt)
{
  for (int i = 0; i < len; i++)
  {
    buf[i] ^= cnt++;
  }
}

/* 4-th byte (image checksum placeholder) calculation */
uint8_t calc_sum(uint8_t *buf, int len)
{
  uint8_t sum = 0;
  
  for (int i = 0; i < len; i++)
  {
    sum += buf[i];
  }
  
  return sum;
}

/* Returns 2 if encoded, 1 if raw, 0 if invalid */
int check_fw(uint8_t *buf, int len)
{
  /* Check for encoded FW */
  /* Check signature and checksum */
  if ((len > 5) && buf[0] == 'm' && buf[1] == 't' && buf[2] == 'c' && calc_sum(buf, len) == 0xFF)
    return 2;
  
  /* Check for decoded FW */
  /* Sanity check for LJMP instruction in first byte (I don't know if it is valid in all cases) */
  if ((len > 1) && buf[0] == 0x02)
    return 1;
  
  /* Nothing matches */  
  return 0;
}

bool decode_buffer(uint8_t **buf, int *len)
{
  /* Check for encoded FW */
  if (check_fw(*buf, *len) != 2)
    _ERRHLP_ML("Your MCU FW image does not looks like encoded MTC FW!\n", EXCEPT);
  
  /* Actually decode here */
  xor_cnt(&(*buf)[4], *len-4, 4);
  
  /* Now here we have a buffer with raw image,
   * so we need to move contents 4 bytes backward
   * and shrink buffer a bit, for 4 bytes of
   * unneeded signature and checksum, so do it */
  memmove(*buf, &(*buf)[4], *len-4);
  *buf = realloc(*buf, *len-4);
  if (*buf == NULL)
    _ERRHLP_ML("Error reallocating buffer!\n", EXCEPT);
  
  *len = *len-4;
  
  return true;
  
EXCEPT:
  return false;
}

bool encode_buffer(uint8_t **buf, int *len)
{
  /* Check for decoded FW */
  if (check_fw(*buf, *len) != 1)
    _ERRHLP_ML("Your MCU FW image does not looks like raw FW!\n", EXCEPT);
  
  /* Now here we have a buffer with raw image,
   * so we need to realloc buffer adding 4 bytes and
   * move contents 4 bytes forward to free space
   * for signature and checksum */
  *buf = realloc(*buf, *len+4);
  if (*buf == NULL)
    _ERRHLP_ML("Error reallocating buffer!\n", EXCEPT);
  memmove(&(*buf)[4], *buf, *len);
  
  /* Actually encode here */
  xor_cnt(&(*buf)[4], *len, 4);
  
  /* Add signature and checksum */
  (*buf)[0] = 'm'; (*buf)[1] = 't'; (*buf)[2] = 'c';
  (*buf)[3] = 0x00;
  (*buf)[3] = 0xFF - calc_sum(*buf, *len+4);
  
  *len = *len+4;
  
  return true;
  
EXCEPT:
  return false;
}

bool load_to_buffer(char *fn, uint8_t **buf, int *len)
{
  FILE *infile = NULL;
  bool result = false;
  
  /* Try to open input file */
  infile = fopen(fn, "rb");
  if (infile == NULL)
    _ERRHLP_ML("Error opening input file!\n", FINALLY);
  
  /* Get file size and check it, not more than max MCU flash,
     not less than something sane */
  fseek(infile, 0, SEEK_END);
  *len = ftell(infile);
  if (*len > 65536 || *len < 256)
    _ERRHLP_ML("Input file is too big or too small, not looks like valid MCU FW image!\n", FINALLY);
  fseek(infile, 0, SEEK_SET);
  
  /* Alloc buffer for all file contents */
  *buf = (uint8_t *) malloc(*len);
  if (*buf == NULL)
    _ERRHLP_ML("Error allocating buffer!\n", FINALLY);

  /* Read FW image in buffer */
  if (fread(*buf, 1, *len, infile) != *len)
    _ERRHLP_ML("Error reading input file!\n", FINALLY);

  result = true;

FINALLY:
  if (infile != NULL)
    fclose(infile);
    
  return result;
}

bool save_from_buffer(uint8_t *buf, int len, char *fn)
{
  FILE *outfile = NULL;
  bool result = false;
  
  /* Try to open output file */
  outfile = fopen(fn, "wb");
  if (outfile == NULL)
    _ERRHLP_ML("Error opening output file!\n", FINALLY);
  
  /* Write all buffer contents to output file */
  if (fwrite(buf, 1, len, outfile) != len)
    _ERRHLP_ML("Error writing output file!\n", FINALLY);
  
  result = true;

FINALLY:
  if (outfile != NULL)
    fclose(outfile);
    
  return result;
}

bool do_decode(char *ifn, char *ofn)
{
  uint8_t *buf = NULL;
  int len = 0;
  bool result = false;
  
  printf("You have selected MCU firmware decode. Good choice! ;)\n\n");
        
  /* Open firmware */
  if (!load_to_buffer(ifn, &buf, &len))
    _ERRHLP_RL(2, FINALLY);
  
  /* Decode firmware */
  if (!decode_buffer(&buf, &len))
    _ERRHLP_RL(2, FINALLY);
  
  /* Save decoded firmware to output file */
  if (!save_from_buffer(buf, len, ofn))
    _ERRHLP_RL(2, FINALLY);
  
  printf("All is done!\n");
  
  result = true;
  
FINALLY:
  if (buf != NULL)
    free(buf);
  
  return result;
}

bool do_encode(char *ifn, char *ofn)
{
  uint8_t *buf = NULL;
  int len = 0;
  bool result = false;
  
  printf("You have selected MCU firmware encode.\n\n");
        
  /* Open firmware */
  if (!load_to_buffer(ifn, &buf, &len))
    _ERRHLP_RL(2, FINALLY);
  
  /* Encode firmware */
  if (!encode_buffer(&buf, &len))
    _ERRHLP_RL(2, FINALLY);
  
  /* Save encoded firmware to output file */
  if (!save_from_buffer(buf, len, ofn))
    _ERRHLP_RL(2, FINALLY);
  
  printf("All is done!\n");
  
  result = true;
  
FINALLY:
  if (buf != NULL)
    free(buf);
  
  return result;
}

bool heuristic_getver(uint8_t *buf, int len, char *verbuf, int verlen)
{
  /* NOTE: Nested functions is GCC extension */
  
  bool extractstr(uint8_t *buf, int len, int *ptr, char *dstr, int dmax)
  {
    int max = (*ptr+dmax > len)?(dmax-((*ptr+dmax)-len)):(dmax);
    memset(dstr, '\0', dmax);
    strncpy(dstr, (char *)&buf[*ptr], max);
    if (dstr[max-1] != '\0') /* Overshot, not looks like our string */
      return false;
    *ptr += strlen(dstr)+1;
    return true;
  }
  
  bool skipzeros(uint8_t *buf, int len, int *ptr, int maxdepth)
  {
    int max = (*ptr+maxdepth > len)?(len):(*ptr+maxdepth);
    for (int i = *ptr; i < max; i++)
    {
      if (buf[i] != '\0')
      {
        *ptr = i;
        return true;
      }
    }
    return false;
  }
  
  char verchunks[6][32];
  int ptr = 0;
  
  /* Firstly try to find needed strings */
  
  /* For modern FWs heuristic works like this:
   * Try to find a first string with formatting and version ("MTC%s-%s%s-VXXX")
   * and then extract all other strings next to it as we know (actually propose)
   * the order how they will go. Simple. */
  for (int i = 0; i < len-5; i++)
  {
    if ((buf[i] == 'M') && (buf[i+1] == 'T') && (buf[i+2] == 'C') && (buf[i+3] == '%') && (buf[i+4] == 's'))
    {
      ptr = i;
      break;
    }
  }
  if (ptr == 0)
    return false;
  
  /* Extract formatter and version string ("MTS%s-%s%s-xxxx") */
  if (!extractstr(buf, len, &ptr, verchunks[0], 32))
    return false;
  /* Skip zeroes (not more than 10) to the beginning of next string */
  if (!skipzeros(buf, len, &ptr, 10))
    return false;
  
  /* Extract some identifier ("B") */
  if (!extractstr(buf, len, &ptr, verchunks[1], 32))
    return false;
  if (!skipzeros(buf, len, &ptr, 10))
    return false;
  /* Additional sanity check for now */
  if (strlen(verchunks[1]) != 1 && verchunks[1][0] != 'B')
    return false;

  /* Extract variant name ("JY", "KGL", etc...) */
  if (!extractstr(buf, len, &ptr, verchunks[2], 32))
    return false;
  if (!skipzeros(buf, len, &ptr, 10))
    return false;
  
  /* Extract date of build */
  if (!extractstr(buf, len, &ptr, verchunks[4], 32))
    return false;
  if (!skipzeros(buf, len, &ptr, 10))
    return false;
  
  /* Extract time of build */
  if (!extractstr(buf, len, &ptr, verchunks[5], 32))
    return false;
  
  /* Get model number for KGL if applicable */
  
  /* For modern FWs heuristic works like this:
   * We need to find a call to bootloader (0x12 EC 00), and then, 
   * if we see moving some XRAM address to DPTR (0x90 aa aa) and right after that
   * moving some const to A (0x74 cc) it is high probablity that we will find
   * KGL variant number in this constant "cc". So, just do it. */
  memset(verchunks[3], '\0', 32);
  if (strncmp(verchunks[2], "KGL", 3) == 0)
  {
    for (int i = 0; i < len-8; i++)
    {
      if ((buf[i] == 0x12) && (buf[i+1] == 0xEC) && (buf[i+2] == 0x00) &&
          (buf[i+3] == 0x90) && (buf[i+6] == 0x74) && ((buf[i+7] >= 0x31) && (buf[i+7] <= 0x35)))
      {
        verchunks[3][0] = buf[i+7];
        break;
      }
    }
  }
  
  /* Output version info */
  int res = snprintf(verbuf, verlen, verchunks[0], verchunks[1], verchunks[2], verchunks[3]);
  if (res < 0 || res >= verlen)
    return true;
  
  /* Output date info */
  strncat(verbuf, "\n", verlen);
  strncat(verbuf, verchunks[4], verlen);
  strncat(verbuf, " ", verlen);
  strncat(verbuf, verchunks[5], verlen);
    
  return true;
}

bool do_getver(char *fn)
{
  uint8_t *buf = NULL;
  int len = 0;
  bool result = false;
  char verinfo[64];
  
  printf("You have selected MCU firmware version search.\n\n");
        
  /* Open firmware */
  if (!load_to_buffer(fn, &buf, &len))
    _ERRHLP_RL(2, FINALLY);
  
  /* Detect FW type and do needed things to decode if encoded */
  switch (check_fw(buf, len))
  {
    case 2: /* Encoded */
    {
      /* Decode */
      printf("Looks like encoded FW image, decoding...\n");
      if (!decode_buffer(&buf, &len))
        _ERRHLP_MRL("Error while decoding!", 2, FINALLY);
      break; /*In case of... */
    }
    case 1: /* Raw */
    {
      /* Do nothing */
      printf("Looks like raw FW image...\n");
      break;
    }
    case 0: default: /* Invalid */
    {
      /* Exit */
      printf("Input file does not looks like FW image!\n");
      _ERRHLP_RL(2, FINALLY);
      break; /* In case of... */
    }
  }
  
  printf("\n");
  
  /* Try to get version information */
  if (!heuristic_getver(buf, len, verinfo, 64))
    _ERRHLP_ML("Can't get version info!", FINALLY);
    
  printf("Version info:\n\n");
  printf(verinfo);
  printf("\n");
  
  result = true;
  
FINALLY:
  if (buf != NULL)
    free(buf);
  
  return result;
}

int main(int argc, char **argv)
{
  printf("\n");
  
  print_banner();
  
  if (argc <= 1)
  {
    printf("Error: insufficient arguments.\n\n");
    print_usage();
    return 1;
  }
  else
  {
    if (_ARGHLP(argv[1], "-d"))
    {
      if (argc == 4)
      {
        if (!do_decode(argv[2], argv[3]))
          return 2;
      }
      else
      {
        printf("Error: insufficient arguments.\n\n");
        return 1;
      }
    }
    else if (_ARGHLP(argv[1], "-e"))
    {
      if (argc == 4)
      {
        if (!do_encode(argv[2], argv[3]))
          return 2;
      }
      else
      {
        printf("Error: insufficient arguments.\n\n");
        return 1;
      }
    }
    else if (_ARGHLP(argv[1], "-v"))
    {
      if (argc == 3)
      {
        if (!do_getver(argv[2]))
          return 2;
      }
      else
      {
        printf("Error: insufficient arguments.\n\n");
        return 1;
      }
    }
    else
    {
      printf("Error: incorrect arguments.\n\n");
      print_usage();
      return 1;
    }
  }
}
