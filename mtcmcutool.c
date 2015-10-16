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
  printf("            Version: 1.0\n");
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

bool do_getver(char *fn)
{
  
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
