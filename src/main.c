/*
 *
 *    main.c
 *    venice
 *    Jean Coupon (2012-2017)
 *
 *    Program that reads a mask file (DS9 type or fits mask) and a catalogue
 *    of objects and computes one of the following tasks:
 *    1. Creates a pixelized mask.
 *    venice -m mask.reg [OPTIONS]
 *    2. Finds objects inside/outside a mask.
 *    venice -m mask.reg -cat file.cat [OPTIONS]
 *    3. Generates a random catalogue of objects inside/outside a mask.
 *    venice -m mask.reg -r [OPTIONS]
 *
 *    TODO:
 *    - adapt to be used with python
 *
 */

#include "main.h"

void testPython(){
   /*    test for python */
   fprintf(stderr,"Hello world\n");
}

int main(int argc, char **argv)
{
   /*    initialization */
   srand((unsigned int)time(NULL));
   EPS   = determineMachineEpsilon();
   IDERR = determineSize_tError();
   Config para;

   /*    tasks */
   switch (readParameters(argc,argv,&para)){
      case 1:
      mask2d(&para);     /* binary mask for visualization */
      break;
      case 2:
      flagCat(&para);    /* objects in/out of mask */
      break;
      case 3:
      randomCat(&para);  /* random catalogue */
      break;
   }
   return EXIT_SUCCESS;
}

/*
 *    Main routines
 */

int  mask2d(const Config *para){
   /*     Returns the mask in gsl histogram format and writes the mask in fileOut.
    *     The limits are the extrema of the extreme polygons in fileRegIn.
    *     The pixel is set to 0 when inside the mask and 1 otherwise.
    *     For fits format, it writes the pixel value.
    */

   int Npolys,poly_id,flag;
   size_t i, j, count, total;
   double x[2], x0[2], xmin[2], xmax[2];

   FILE *fileOut = fopenAndCheck(para->fileOutName,"w");

   gsl_histogram2d *mask = gsl_histogram2d_alloc(para->nx,para->ny);
   total = para->nx*para->ny;

   if(checkFileExt(para->fileRegInName,".fits")){           /*     fits file */
      long fpixel[2], naxes[2];
      int bitpix, status = 0;
      double (*convert)(void *,long ) = NULL;

      /*   read fits file and put in table */
      void *table = readFits(para,&bitpix,&status,naxes,&convert);

      /* define limits */
      xmin[0] = xmin[1] = 1.0;
      xmax[0] = naxes[0];
      xmax[1] = naxes[1];
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /*    print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      gsl_histogram2d_set_ranges_uniform(mask,xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    ATTENTION "convert" converts everything into double */
      fprintf(stderr,"\nProgress =     ");
      for(i=0; i<mask->nx; i++){
         for(j=0; j<mask->ny; j++){
            count = i*para->ny+j;
            printCount(&count,&total,1000);
            x[0] = (mask->xrange[i]+mask->xrange[i+1])/2.0;  /* center of the pixel */
            x[1] = (mask->yrange[j]+mask->yrange[j+1])/2.0;
            fpixel[0] = roundToNi(x[0]) - 1;
            fpixel[1] = roundToNi(x[1]) - 1;
            mask->bin[i*mask->ny+j] = convert(table,fpixel[1]*naxes[0]+fpixel[0]);
         }
      }
      fprintf(stderr,"\b\b\b\b100%%\n");

      free(table);

   }else if(checkFileExt(para->fileRegInName,".reg")){          /* ds9 file */

      FILE *fileRegIn = fopenAndCheck(para->fileRegInName,"r");
      Node *polyTree  = readPolygonFileTree(fileRegIn,xmin,xmax);
      Polygon *polys  = (Polygon *)polyTree->polysAll;
      Npolys          = polyTree->Npolys;
      fclose(fileRegIn);

      /*    define limits */
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /*    print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    reference point. It must be outside the mask */
      x0[0] = xmin[0] - 1.0; x0[1] = xmin[1] - 1.0;

      gsl_histogram2d_set_ranges_uniform(mask,xmin[0],xmax[0],xmin[1],xmax[1]);

      total = para->nx*para->ny;

      fprintf(stderr,"Progress =     ");
      for(i=0; i<mask->nx; i++){
         for(j=0; j<mask->ny; j++){
            count = i*para->ny+j;
            printCount(&count,&total,1000);
            x[0] = (mask->xrange[i]+mask->xrange[i+1])/2.0; /* center of the pixel */
            x[1] = (mask->yrange[j]+mask->yrange[j+1])/2.0;
            /* 1 = outside the mask, 0 = inside the mask */
            if(!insidePolygonTree(polyTree,x0,x,&poly_id)) mask->bin[i*mask->ny+j] = 1.0;
         }
      }
      fflush(stdout);
      fprintf(stderr,"\b\b\b\b100%%\n");
   }else{
      fprintf(stderr,"%s: mask file format not recognized. Please provide .reg, .fits or no mask but input limits. Exiting...\n",MYNAME);
      exit(EXIT_FAILURE);
   }

   /*    write the output file */
   for(j=0; j<mask->ny; j++){
      for(i=0; i<mask->nx; i++){
         fprintf(fileOut,"%g ",mask->bin[i*mask->ny+j]);
      }
      fprintf(fileOut,"\n");
   }
   fclose(fileOut);

   return EXIT_SUCCESS;
}

int flagCat(const Config *para){
   /*
    *    Reads fileCatIn and add a flag at the end of the line. 1 is outside
    *    the mask and 0 is inside the mask. xcol and ycol are the column ids
    *    of resp. x coordinate and y coordinate.
    *    For fits format, it writes the pixel value.
    */

   int Npolys, poly_id, flag, verbose = 1;
   size_t i,N, Ncol;
   double x[2], x0[2], xmin[2], xmax[2];
   char line[NFIELD*NCHAR], item[NFIELD*NCHAR],*str_end;

   FILE *fileOut   = fopenAndCheck(para->fileOutName,"w");
   FILE *fileCatIn = fopenAndCheck(para->fileCatInName,"r");

   N = 0;
   if(fileCatIn != stdin){
      while(fgets(line,NFIELD*NCHAR,fileCatIn) != NULL)
      if(getStrings(line,item," ",&Ncol))  N++;
      rewind(fileCatIn);
      fprintf(stderr,"Nobjects = %zd\n", N);
   }else{
      verbose = 0;
   }

   if(checkFileExt(para->fileRegInName,".fits")){
      if(para->coordType != CART){
         fprintf(stderr,"%s: fits file detected. coord should be set to cart for image coordinates. Exiting...\n",MYNAME);
         exit(EXIT_FAILURE);
      }

      long fpixel[2], naxes[2];
      int bitpix, status = 0;
      double (*convert)(void *,long ) = NULL;

      /*    read fits file and put in table */
      void *table = readFits(para,&bitpix,&status,naxes,&convert);

      /*    define limits */
      xmin[0] = xmin[1] = 1.0;
      xmax[0] = naxes[0];
      xmax[1] = naxes[1];
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /*    print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    ATTENTION "convert" converts everything into double */
      i = 0;
      if(verbose) fprintf(stderr,"\nProgress =     ");
      while(fgets(line,NFIELD*NCHAR,fileCatIn) != NULL){

         /*    keep commented lines */
         if (line[0] == '#') fprintf(fileOut,"%s",line);

         if(getStrings(line,item," ",&Ncol)){
            i++;
            if(verbose) printCount(&i,&N,1000);
            x[0] = getDoubleValue(item,para->xcol);
            x[1] = getDoubleValue(item,para->ycol);
            fpixel[0] = roundToNi(x[0]) - 1;
            fpixel[1] = roundToNi(x[1]) - 1;
            str_end = strstr(line,"\n");/* cariage return to the end of the line */
            strcpy(str_end,"\0");       /* "end" symbol to the line              */
            if(xmin[0] < x[0] && x[0] < xmax[0] && xmin[1] < x[1] && x[1] < xmax[1]){
               fprintf(fileOut,"%s %g\n",line,convert(table,fpixel[1]*naxes[0]+fpixel[0]));
            }else{
               fprintf(fileOut,"%s %d\n",line,-99);
            }
         }
      }
      if(verbose) fprintf(stderr,"\b\b\b\b100%%\n");

      free(table);

   }else if(checkFileExt(para->fileRegInName,".reg")){
      FILE *fileRegIn = fopenAndCheck(para->fileRegInName,"r");
      Node *polyTree  = readPolygonFileTree(fileRegIn,xmin,xmax);
      Polygon *polys = (Polygon *)polyTree->polysAll;
      Npolys          = polyTree->Npolys;
      fclose(fileRegIn);

      /*    or if the limits are defined by the user */
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /* print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    reference point. It must be outside the mask */
      x0[0] = xmin[0] - 1.0; x0[1] = xmin[1] - 1.0;

      i = 0;
      if(verbose) fprintf(stderr,"Progress =     ");
      while(fgets(line,NFIELD*NCHAR,fileCatIn) != NULL){

         /*    keep commented lines */
         if (line[0] == '#') fprintf(fileOut,"%s",line);

         if(getStrings(line,item," ",&Ncol)){
            i++;
            if(verbose) printCount(&i,&N,1000);
            x[0] = getDoubleValue(item,para->xcol);
            x[1] = getDoubleValue(item,para->ycol);

            if(flag=0, !insidePolygonTree(polyTree,x0,x,&poly_id)) flag = 1;

            str_end = strstr(line,"\n");/*   cariage return to the end of the line */
            strcpy(str_end,"\0");       /*   "end" symbol to the line */
            switch (para->format){
               case 1: /*  only objects outside the mask and inside the user's definied limits */
               if(flag) fprintf(fileOut,"%s\n",line);
               break;
               case 2: /*  only objects inside the mask or outside the user's definied limits */
               if(!flag) fprintf(fileOut,"%s\n",line);
               break;
               case 3: /*  all objects with the flag */
               fprintf(fileOut,"%s %d\n",line,flag);
            }
         }
      }
      fflush(stdout);
      if(verbose) fprintf(stderr,"\b\b\b\b100%%\n");
   }else{
      fprintf(stderr,"%s: mask file format not recognized. Please provide .reg, .fits or no mask with input limits. Exiting...\n",MYNAME);
      exit(EXIT_FAILURE);
   }

   fclose(fileOut);
   fclose(fileCatIn);

   return(EXIT_SUCCESS);
}

int randomCat(const Config *para){
   /*    Generates a random catalogue inside the mask (uniform PDF).
    *    If "all", it puts all objects and add a flag such as:
    *    outside the mask:1, inside the mask:0. Otherwise it puts only objects
    *    outside the mask.
    */

   int Npolys,poly_id,flag, verbose = 1;
   size_t i, npart;
   double x[2], x0[2], xmin[2], xmax[2], z, area;
   gsl_rng *r = randomInitialize(para->seed);

   FILE *fileOut = fopenAndCheck(para->fileOutName,"w");

   /*    load redshift distribution */
   /*    GSL convention: bin[i] corresponds to range[i] <= x < range[i+1] */
   FILE *fileNofZ;
   size_t Nbins, Ncol;
   char line[NFIELD*NCHAR], item[NFIELD*NCHAR];

   gsl_histogram *nz;
   gsl_histogram_pdf *nz_PDF;

   /* DEBUGGING
   for(i=0;i<2000;i++){
   z = (double)i/1000.0;
   printf("%f %f %f\n",z,dvdz(z,para->a),distComo(z,para->a)*distComo(z,para->a)*distComo(z,para->a));
   }
   exit(-1);
   */

   if(para->nz){
      fileNofZ = fopenAndCheck(para->fileNofZName,"r");
      Nbins= 0;
      while(fgets(line,NFIELD*NCHAR, fileNofZ) != NULL)
      if(getStrings(line,item," ",&Ncol))  Nbins++;
      rewind(fileNofZ);
      fprintf(stderr,"Nbins = %zd\n",Nbins);

      nz     = gsl_histogram_alloc(Nbins);
      nz_PDF = gsl_histogram_pdf_alloc (Nbins);

      i = 0;
      while(fgets(line,NFIELD*NCHAR, fileNofZ) != NULL){
         if(getStrings(line,item," ",&Ncol)){
            nz->range[i] = getDoubleValue(item, 1);
            nz->bin[i]   = getDoubleValue(item, 2);
            i++;
         }
      }
      /*    gsl structure requires an upper limit for the last bin*/
      nz->range[i]   = 100.0;
      gsl_histogram_pdf_init (nz_PDF, nz);
   }

   if(para->zrange){
      Nbins= 1000;
      double delta = (para->zmax - para->zmin)/(double)Nbins;

      nz     = gsl_histogram_alloc(Nbins);
      nz_PDF = gsl_histogram_pdf_alloc (Nbins);

      for(i=0;i<Nbins;i++){
         nz->range[i] = para->zmin + delta*(double)i;
         nz->bin[i]   = dvdz(nz->range[i] + delta/2.0, para->a);
      }
      /*    gsl structure requires an upper limit for the last bin*/
      nz->range[Nbins]   = para->zmin + delta*(double)Nbins;
      gsl_histogram_pdf_init (nz_PDF, nz);
   }

   if(checkFileExt(para->fileRegInName,".fits")){
      if(para->coordType != CART){
         fprintf(stderr,"%s: fits file detected. coord should be set to cart for image coordinates. Exiting...\n",MYNAME);
         exit(EXIT_FAILURE);
      }

      long fpixel[2], naxes[2];
      int bitpix, status = 0;
      double (*convert)(void *,long ) = NULL;

      /*    read fits file and put in table */
      void *table = readFits(para,&bitpix,&status,naxes,&convert);

      /*    define limits */
      xmin[0] = xmin[1] = 1.0;
      xmax[0] = naxes[0];
      xmax[1] = naxes[1];
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /*    print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    ATTENTION "convert" converts everything into double */
      fprintf(stderr,"\nProgress =     ");
      for(i=0;i<para->npart;i++){
         printCount(&i,&para->npart,1000);
         x[0] = gsl_ran_flat(r,xmin[0],xmax[0]);
         x[1] = gsl_ran_flat(r,xmin[1],xmax[1]);
         fpixel[0] = roundToNi(x[0]) - 1;
         fpixel[1] = roundToNi(x[1]) - 1;
         if(para->nz || para->zrange){
            z = gsl_histogram_pdf_sample (nz_PDF, gsl_ran_flat(r, 0.0, 1.0));
            fprintf(fileOut,"%f %f %g %f\n", x[0], x[1], convert(table,fpixel[1]*naxes[0]+fpixel[0]), z);
         }else{
            fprintf(fileOut,"%f %f %g\n", x[0], x[1], convert(table,fpixel[1]*naxes[0]+fpixel[0]));
         }
      }
      fprintf(stderr,"\b\b\b\b100%%\n");

      free(table);

   }else if(checkFileExt(para->fileRegInName,".reg")){

      FILE *fileRegIn = fopenAndCheck(para->fileRegInName,"r");
      Node *polyTree  = readPolygonFileTree(fileRegIn,xmin,xmax);
      Polygon *polys = (Polygon *)polyTree->polysAll;
      Npolys          = polyTree->Npolys;
      fclose(fileRegIn);

      /*    or if the limits are defined by the user */
      if(para->minDefinied[0]) xmin[0] = para->min[0];
      if(para->maxDefinied[0]) xmax[0] = para->max[0];
      if(para->minDefinied[1]) xmin[1] = para->min[1];
      if(para->maxDefinied[1]) xmax[1] = para->max[1];
      /* print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);

      /*    reference point. It must be outside the mask */
      x0[0] = xmin[0] - 1.0; x0[1] = xmin[1] - 1.0;

      //fprintf(stderr,"xmin = %f \nxmax = %f \nymin = %f \nymax = %f\n",xmin[0],xmax[0],xmin[1],xmax[1]);
      if(para->coordType == RADEC){
         area = (xmax[0] - xmin[0])*(sin(xmax[1]*PI/180.0) - sin(xmin[1]*PI/180.0))*180.0/PI;
      }else{
         area = (xmax[0] - xmin[0])*(xmax[1] - xmin[1]);
      }
      fprintf(stderr, "Area = %f\n", area);
      fprintf(fileOut,"# %f\n", area);

      if(para->constDen){
         npart =  (size_t)round((double)para->npart*area);
      }else{
         npart = para->npart;
      }
      fprintf(stderr,"Creates a random catalogue with N = %zd objects. Format = %d\n",npart,para->format);

      fprintf(stderr,"Progress =     ");
      for(i=0;i<npart;i++){
         printCount(&i,&npart,1000);
         if(para->coordType == CART){
            x[0] = gsl_ran_flat(r,xmin[0],xmax[0]);
            x[1] = gsl_ran_flat(r,xmin[1],xmax[1]);
         }else{
            x[0] = gsl_ran_flat(r,xmin[0],xmax[0]);
            x[1] = gsl_ran_flat(r,sin(xmin[1]*PI/180.0),sin(xmax[1]*PI/180.0));
            x[1] = asin(x[1])*180.0/PI;
         }
         /*    1 = outside the mask, 0 = inside the mask */
         if(flag=0,!insidePolygonTree(polyTree,x0,x,&poly_id)) flag = 1;
         if(para->nz || para->zrange){
            z = gsl_histogram_pdf_sample (nz_PDF, gsl_ran_flat(r, 0.0, 1.0));
            switch (para->format){
               case 1: /* only objects outside the mask */
               if(flag) fprintf(fileOut,"%f %f %f\n",x[0],x[1],z);
               break;
               case 2: /* only objects inside the mask */
               if(!flag) fprintf(fileOut,"%f %f %f\n",x[0],x[1],z);
               break;
               case 3: /* all objects with the flag */
               fprintf(fileOut,"%f %f %d %f\n",x[0],x[1],flag,z);
            }
         }else{
            switch (para->format){
               case 1: /* only objects outside the mask */
               if(flag) fprintf(fileOut,"%f %f\n",x[0],x[1]);
               break;
               case 2: /* only objects inside the mask */
               if(!flag) fprintf(fileOut,"%f %f\n",x[0],x[1]);
               break;
               case 3: /* all objects with the flag */
               fprintf(fileOut,"%f %f %d\n",x[0],x[1],flag);
            }
         }
      }
      fprintf(stderr,"\b\b\b\b100%%\n");
   }else if(!strcmp(para->fileRegInName,"\0")){

      fprintf(stderr,"Generating catalogue with no mask...\n");
      xmin[0] = para->min[0];
      xmax[0] = para->max[0];
      xmin[1] = para->min[1];
      xmax[1] = para->max[1];
      /*    print out limits */
      fprintf(stderr,"limits:\n");
      fprintf(stderr,"-xmin %g -xmax %g -ymin %g -ymax %g\n",xmin[0],xmax[0],xmin[1],xmax[1]);
      if(para->coordType == RADEC){
         area = (xmax[0] - xmin[0])*(sin(xmax[1]*PI/180.0) - sin(xmin[1]*PI/180.0))*180.0/PI;
      }else{
         area = (xmax[0] - xmin[0])*(xmax[1] - xmin[1]);
      }
      fprintf(stderr, "Area = %f\n", area);
      fprintf(fileOut, "# %f\n", area);

      if(para->constDen){
         npart = (size_t)round((double)para->npart*area);
      }else{
         npart = para->npart;
      }
      fprintf(stderr,"Creates a random catalogue with N = %zd objects.\n",npart);
      fprintf(stderr,"Progress =     ");
      for(i=0;i<npart;i++){
         printCount(&i,&npart,1000);
         if(para->coordType == CART){
            x[0] = gsl_ran_flat(r,xmin[0],xmax[0]);
            x[1] = gsl_ran_flat(r,xmin[1],xmax[1]);
         }else{
            x[0] = gsl_ran_flat(r,xmin[0],xmax[0]);
            x[1] = gsl_ran_flat(r,sin(xmin[1]*PI/180.0),sin(xmax[1]*PI/180.0));
            x[1] = asin(x[1])*180.0/PI;
         }
         if(para->nz || para->zrange){
            z = gsl_histogram_pdf_sample (nz_PDF, gsl_ran_flat(r, 0.0, 1.0));
            fprintf(fileOut,"%f %f %f\n", x[0], x[1], z);
         }else{
            fprintf(fileOut,"%f %f\n", x[0], x[1]);
         }
      }
      fflush(stdout);
      fprintf(stderr,"\b\b\b\b100%%\n");
      return(EXIT_SUCCESS);

   }else{

      fprintf(stderr,"%s: mask file format not recognized. Please provide .reg, .fits or no mask with input limits. Exiting...\n",MYNAME);
      exit(EXIT_FAILURE);

   }

   if(para->nz || para->zrange){
      gsl_histogram_pdf_free (nz_PDF);
      gsl_histogram_free (nz);
   }

   fclose(fileOut);
   return(EXIT_SUCCESS);
}