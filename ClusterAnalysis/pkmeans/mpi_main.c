/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*   File:         mpi_main.c   (an MPI version)                             */
/*   Description:  This program shows an example on how to call a subroutine */
/*                 that implements a simple k-means clustering algorithm     */
/*                 based on Euclid distance.                                 */
/*   Input file format:                                                      */
/*                 ascii  file: each line contains 1 data object             */
/*                 binary file: first 4-byte integer is the number of data   */
/*                 objects and 2nd integer is the no. of features (or        */
/*                 coordinates) of each object                               */
/*                                                                           */
/*   Author:  Wei-keng Liao                                                  */
/*            ECE Department Northwestern University                         */
/*            email: wkliao@ece.northwestern.edu                             */
/*                                                                           */
/*   Copyright (C) 2005, Northwestern University                             */
/*   See COPYRIGHT notice in top-level directory.                            */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* strtok() */
#include <sys/types.h>  /* open() */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>     /* getopt() */

#include <mpi.h>
int      _debug;
#include "kmeans.h"

#ifdef _PNETCDF_BUILT
#include <pnetcdf.h>
float** pnetcdf_read(char*, char*, int*, int*, MPI_Comm);
int     pnetcdf_write(char*, int, int, int, int, float**, int*, int, MPI_Comm,
                      int verbose);
#endif

int     mpi_kmeans(float**, int, int, int, float, int*, float**, MPI_Comm);
float** mpi_read(int, char*, int*, int*, MPI_Comm);
int     mpi_write(int, char*, int, int, int, float**, int*, int, MPI_Comm,
                  int verbose);


/*---< usage() >------------------------------------------------------------*/
static void usage(char *argv0, float threshold) {
    char *help =
// TODO >> modified by VL: added new option "-c centers_filename "
		"Usage: %s [switches] -i filename -c centers_filename -n num_clusters\n"
// TODO << end of the modification
        "       -i filename    : file containing data to be clustered\n"
// TODO >> modified by VL: added new option
		"       -z centers_filename    : file containing the initial centers (default: first num_clusters objects)\n"
// TODO << end of the modification
        "       -b             : input file is in binary format (default no)\n"
        "       -r             : output file in binary format (default no)\n"
        "       -n num_clusters: number of clusters (K must > 1)\n"
        "       -t threshold   : threshold value (default %.4f)\n"
        "       -o             : output timing results (default no)\n"
        "       -c var_name    : using PnetCDF for file input and output and var_name\n"
        "                      : is variable name in the netCDF file to be clustered\n"
        "       -q             : quiet mode\n"
        "       -d             : enable debug mode\n"
        "       -h             : print this help information\n";
    fprintf(stderr, help, argv0, threshold);
}

/*---< main() >-------------------------------------------------------------*/
int main(int argc, char **argv) {
           int     opt;
    extern char   *optarg;
    extern int     optind;
           int     i, j;
           int     isInFileBinary, isOutFileBinary, do_pnetcdf;
           int     is_output_timing, is_print_usage, verbose;

           int     numClusters, numCoords, numObjs, totalNumObjs;
           int    *membership;    /* [numObjs] */
           char   *filename;
// TODO >> modified by VL: new variable representing the centers file name
		  char   *centers_filename;
// TODO << end of the modification
           char   *var_name;
           float **objects;       /* [numObjs][numCoords] data objects */
           float **clusters;      /* [numClusters][numCoords] cluster center */
           float   threshold;
           double  timing, io_timing, clustering_timing;

           int     rank, nproc, mpi_namelen;
           char    mpi_name[MPI_MAX_PROCESSOR_NAME];

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
    MPI_Get_processor_name(mpi_name,&mpi_namelen);

    /* some default values */
    _debug           = 0;
    verbose          = 1;
    threshold        = 0.001;
    numClusters      = 0;
    isInFileBinary   = 0;
    isOutFileBinary  = 0;
    is_output_timing = 0;
    is_print_usage   = 0;
    filename         = NULL;
// TODO >> modified by VL: initialization of the new variable
	centers_filename = NULL;
// TODO << end of the modification
    do_pnetcdf       = 0;
    var_name         = NULL;

// TODO >> modified by VL: added the letter z
    while ( (opt=getopt(argc,argv,"i:z:n:t:v:c:abdorhq"))!= EOF) {
// TODO << end of the modification
       switch (opt) {
            case 'i': filename=optarg;
                      break;
// TODO >> modified by VL: initialize centers filename
            case 'z': centers_filename=optarg;
				  	  break;
// TODO << end of the modification
            case 'b': isInFileBinary = 1;
                      break;
            case 'r': isOutFileBinary = 1;
                      break;
            case 't': threshold=atof(optarg);
                      break;
            case 'n': numClusters = atoi(optarg);
                      break;
            case 'o': is_output_timing = 1;
                      break;
            case 'c': do_pnetcdf = 1;
                      var_name = optarg;
                      break;
            case 'q': verbose = 0;
                      break;
            case 'd': _debug = 1;
                      break;
            case 'h':
            default: is_print_usage = 1;
                      break;
        }
    }

    if (filename == 0 || numClusters <= 1 || is_print_usage == 1 ||
        (do_pnetcdf && var_name == NULL)) {
        if (rank == 0) usage(argv[0], threshold);
        MPI_Finalize();
        exit(1);
    }

    if (_debug) printf("Proc %d of %d running on %s\n", rank, nproc, mpi_name);

#ifndef _PNETCDF_BUILT
    if (do_pnetcdf) {
        if (rank == 0) printf("Error: PnetCDF feature is not built\n");
        MPI_Finalize();
        exit(1);
    }
#endif

    MPI_Barrier(MPI_COMM_WORLD);
    io_timing = MPI_Wtime();

    /* read data points from file ------------------------------------------*/
#ifdef _PNETCDF_BUILT
    if (do_pnetcdf)
        objects = pnetcdf_read(filename, var_name, &numObjs, &numCoords,
                               MPI_COMM_WORLD);
    else
#endif
        objects = mpi_read(isInFileBinary, filename, &numObjs, &numCoords,
                           MPI_COMM_WORLD);

    if (_debug) { /* print the first 4 objects' coordinates */
        int num = (numObjs < 4) ? numObjs : 4;
        for (i=0; i<num; i++) {
            char strline[1024], strfloat[16];
            sprintf(strline,"%d: objects[%d]= ",rank,i);
            for (j=0; j<numCoords; j++) {
                sprintf(strfloat,"%10f",objects[i][j]);
                strcat(strline, strfloat);
            }
            strcat(strline, "\n");
            printf("%s",strline);
        }
    }

    timing            = MPI_Wtime();
    io_timing         = timing - io_timing;
    clustering_timing = timing;

    /* allocate a 2D space for clusters[] (coordinates of cluster centers)
       this array should be the same across all processes                  */
    clusters    = (float**) malloc(numClusters *             sizeof(float*));
    assert(clusters != NULL);
    clusters[0] = (float*)  malloc(numClusters * numCoords * sizeof(float));
    assert(clusters[0] != NULL);
    for (i=1; i<numClusters; i++)
        clusters[i] = clusters[i-1] + numCoords;

    MPI_Allreduce(&numObjs, &totalNumObjs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    /* checking if numObjs < nproc is done in the I/O routine */

// TODO >> modified by VL: initialize variable "clusters"
	// possibly load the centers from a file
	if (centers_filename != NULL)
	{	int num_centers, num_coords;
		clusters = file_read(0, centers_filename, &num_centers, &num_coords);
		// no control over the numbers of centers and coordinates
	}
	// otherwise, pick the first numClusters elements of objects[] as initial cluster centers
	else
// TODO << end of the modification

		/* pick first numClusters elements in feature[] as initial cluster centers*/
		if (rank == 0) {
			if (numObjs < numClusters) {
				/* read the first numClusters data points from file */
				read_n_objects(isInFileBinary, filename, numClusters, numCoords, clusters);
			}
			else {
				/* copy the first numClusters elements in feature[] */
				for (i=0; i<numClusters; i++)
					for (j=0; j<numCoords; j++)
						clusters[i][j] = objects[i][j];
			}
		}
    MPI_Bcast(clusters[0], numClusters*numCoords, MPI_FLOAT, 0, MPI_COMM_WORLD);

    /* membership: the cluster id for each data object */
    membership = (int*) malloc(numObjs * sizeof(int));
    assert(membership != NULL);

    /* start the core computation -------------------------------------------*/
    mpi_kmeans(objects, numCoords, numObjs, numClusters, threshold, membership,
               clusters, MPI_COMM_WORLD);

    free(objects[0]);
    free(objects);

    timing            = MPI_Wtime();
    clustering_timing = timing - clustering_timing;

    /* output: the coordinates of the cluster centres ----------------------*/
#ifdef _PNETCDF_BUILT
    if (do_pnetcdf)
        pnetcdf_write(filename, 1, numClusters, numObjs, numCoords, clusters,
                      membership, totalNumObjs, MPI_COMM_WORLD, verbose);
    else
#endif
        mpi_write(isOutFileBinary, filename, numClusters, numObjs, numCoords,
                  clusters, membership, totalNumObjs, MPI_COMM_WORLD, verbose);

    free(membership);
    free(clusters[0]);
    free(clusters);

    /*---- output performance numbers ---------------------------------------*/
    if (is_output_timing) {
        double max_io_timing, max_clustering_timing;

        io_timing += MPI_Wtime() - timing;

        /* get the max timing measured among all processes */
        MPI_Reduce(&io_timing, &max_io_timing, 1, MPI_DOUBLE,
                   MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&clustering_timing, &max_clustering_timing, 1, MPI_DOUBLE,
                   MPI_MAX, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            printf("\nPerforming **** Simple Kmeans  (MPI) ****\n");
            printf("Num of processes = %d\n", nproc);
            printf("Input file:        %s\n", filename);
// TODO >> modified by VL: display centers filename
		if(centers_filename!=NULL)
			printf("Centers file:     %s\n", centers_filename);
// TODO << end of the modifications
            printf("numObjs          = %d\n", totalNumObjs);
            printf("numCoords        = %d\n", numCoords);
            printf("numClusters      = %d\n", numClusters);
            printf("threshold        = %.4f\n", threshold);

            printf("I/O time           = %10.4f sec\n", max_io_timing);
            printf("Computation timing = %10.4f sec\n", max_clustering_timing);
        }
    }

    MPI_Finalize();
    return(0);
}

