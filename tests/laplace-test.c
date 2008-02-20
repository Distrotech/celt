#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "laplace.h"

int main()
{
   int i;
   int ret = 0;
   ec_enc enc;
   ec_dec dec;
   ec_byte_buffer buf;
   int val[10000], decay[10000];
   ec_byte_writeinit(&buf);
   ec_enc_init(&enc,&buf);
   
   for (i=0;i<10000;i++)
   {
      val[i] = rand()%15-7;
      decay[i] = rand()%11000+5000;
      ec_laplace_encode(&enc, val[i], decay[i]);      
   }
      
   ec_enc_done(&enc);

   ec_byte_readinit(&buf,ec_byte_get_buffer(&buf),ec_byte_bytes(&buf));
   ec_dec_init(&dec,&buf);

   for (i=0;i<10000;i++)
   {
      int d = ec_laplace_decode(&dec, decay[i]);
      if (d != val[i])
      {
         fprintf (stderr, "Got %d instead of %d\n", d, val[i]);
         ret = 1;
      }
   }
   
   ec_byte_writeclear(&buf);
   return ret;
}