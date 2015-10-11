#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define _ARGHLP(s) s, strlen(s)
#define _ERRHLP(m, c, l) { printf(m); exitcode = c; goto l; } 

int exitcode = 0;

void print_banner()
{
  printf("mtcmcutool: a tool to manipulate MTC MCU firmware images for RK3066/RK3188 headunits.\n");
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
#ifdef OPT_VER
  printf("\n");
  printf("       mtcmcutool -v <file name>\n");
  printf("         to try to heuristically determine and print MCU version, where:\n");
  printf("           <file name> is encoded or plain MCU image file name that you\n");
  printf("                       want to try to extract version information from.\n");
#endif
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

int main(int argc, char **argv)
{
  FILE *infile = NULL;
  FILE *outfile = NULL;
  int inlen;
  uint8_t *buf = NULL;
  
  printf("\n");
  
  print_banner();
  
  if (argc <= 1)
  {
    printf("Error: insufficient arguments.\n\n");
    print_usage();
    exitcode = 1;
  }

  if (!strncmp(argv[1], _ARGHLP("-d")))
  {
    if (argc == 4)
    {
      printf("You have selected MCU firmware decode. Good choice! ;)\n\n");
      
      /* Try to open input file */
      infile = fopen(argv[2], "rb");
      if (infile == NULL)
        _ERRHLP("Error opening input file!\n", 2, FINALLY_A);
      
      /* Get file size */
      fseek(infile, 0, SEEK_END);
      inlen = ftell(infile);
      if (inlen > 65536)
        _ERRHLP("Input file is too big, not looks like valid MCU FW image!\n", 2, FINALLY_A);
      fseek(infile, 0, SEEK_SET);
      
      /* Alloc buffer for all FW image */
      buf = (uint8_t *) malloc(inlen);
      if (buf == NULL)
        _ERRHLP("Error allocating buffer!\n", 2, FINALLY_A);
      
      /* Read all FW image in buffer */
      if (fread(buf, 1, inlen, infile) != inlen)
        _ERRHLP("Error reading input file!\n", 2, FINALLY_A);
      
      /* Check signature and checksum */
      if (buf[0] != 'm' || buf[1] != 't' || buf[2] != 'c' || calc_sum(buf, inlen) != 0xFF)
        _ERRHLP("Your MCU FW image is invalid or corrupted (signature invalid or checksum error)!\n", 2, FINALLY_A);
      
      uint8_t *wbuf = &buf[4]; /* We need to start decoding skipping first */
      int wlen = inlen-4;      /* 4 bytes with signature and checksum */
      
      /* Actually decode here */
      xor_cnt(wbuf, wlen, 4);
      
      /* Try to open output file */
      outfile = fopen(argv[3], "wb");
      if (outfile == NULL)
        _ERRHLP("Error opening output file!\n", 2, FINALLY_A);
      
      /* Write decoded FW to output file */
      if (fwrite(wbuf, 1, wlen, outfile) != wlen)
        _ERRHLP("Error writing output file!\n", 2, FINALLY_A);
      
      printf("All is done!\n");
      
FINALLY_A:
      if (outfile != NULL)
        fclose(outfile);
      if (buf != NULL)
        free(buf);
      if (infile != NULL)
        fclose(infile);
    }
    else
    {
      printf("Error: insufficient arguments.\n\n");
      /*print_help();*/
      exitcode = 1;
    }
  }
  else if (!strncmp(argv[1], _ARGHLP("-e")))
  {
    if (argc == 4)
    {
      printf("You have selected MCU firmware encode.\n\n");
      
      /* Try to open input file */
      infile = fopen(argv[2], "rb");
      if (infile == NULL)
        _ERRHLP("Error opening input file!\n", 2, FINALLY_B);
      
      /* Get file size */
      fseek(infile, 0, SEEK_END);
      inlen = ftell(infile);
      if (inlen > 65536)
        _ERRHLP("Input file is too big, not looks like valid MCU FW image!\n", 2, FINALLY_B);
      fseek(infile, 0, SEEK_SET);
      
      int wlen = inlen+4; /* as we need extra space for signature and checksum */
      
      /* Alloc buffer for all FW image */
      buf = (uint8_t *) malloc(wlen);
      if (buf == NULL)
        _ERRHLP("Error allocating buffer!\n", 2, FINALLY_B);
      
      uint8_t *wbuf = &buf[4]; /* We need to start encoding skipping first 4 bytes */
      
      /* Read all FW image in buffer */
      if (fread(wbuf, 1, inlen, infile) != inlen)
        _ERRHLP("Error reading input file!\n", 2, FINALLY_B);
      
      /* Actually encode here */
      xor_cnt(wbuf, inlen, 4);
      
      /* Add signature and checksum */
      buf[0] = 'm'; buf[1] = 't'; buf[2] = 'c';
      buf[3] = 0x00;
      buf[3] = 0xFF - calc_sum(buf, wlen);
      
      /* Try to open output file */
      outfile = fopen(argv[3], "wb");
      if (outfile == NULL)
        _ERRHLP("Error opening output file!\n", 2, FINALLY_B);
      
      /* Write decoded FW to output file */
      if (fwrite(buf, 1, wlen, outfile) != wlen)
        _ERRHLP("Error writing output file!\n", 2, FINALLY_B);
      
      printf("All is done!\n");
      
FINALLY_B:
      if (outfile != NULL)
        fclose(outfile);
      if (buf != NULL)
        free(buf);
      if (infile != NULL)
        fclose(infile);
    }
    else
    {
      printf("Error: insufficient arguments.\n\n");
      /*print_help();*/
      exitcode = 1;
    }
  }
#ifdef OPT_VER
  else if (!strncmp(argv[1], _ARGHLP("-v")))
  {
    if (argc == 3)
    {
      /* Version detection is not completed for this moment */
    }
    else
    {
      printf("Error: insufficient arguments.\n\n");
      /*print_help();*/
      exitcode = 1;
    }
  }
#endif
  else
  {
    printf("Error: incorrect arguments.\n\n");
    print_usage();
    exitcode = 1;
  }
  
  return exitcode;
}
