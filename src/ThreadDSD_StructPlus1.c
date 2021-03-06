#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include "StructPlus.h"
#include "ParseCommandLine.h"

#define F 89 // d
#define T 1 // s
#define NUM_THREADS 4

/**
 * Driver that evaluates test instances using the StructPlus
 * implementation. Use the following command to run this driver:
 *
 * ./StructPlus -ensemble <ensemble-path> -instances <test-instances-path> \
 *              -maxLeaves <max-number-of-leaves> [-print]
 *
 */

int numberOfInstances;
int nbTrees;
StructPlus** trees;
float** features;
int printScores;

FILE *fp_log; // log file
float thread_sum[NUM_THREADS]; // save the sum result from each thread

void *BusyWork(void *t)
{
  long tid;
  tid = (long)t;
  fprintf(fp_log, "Thread %ld starting...\n", tid);
  float score;
  float sum=0;
  int iIndex, tindex, i, j;
  int begin_index, end_index, divide;
  divide = numberOfInstances / NUM_THREADS;
  begin_index = (int)tid * divide;
  if (tid == NUM_THREADS-1)
    end_index = numberOfInstances;
  else
    end_index = (int)(tid+1) * divide;
  
  //DSDS
  int remainder, remainder_F;
  remainder = nbTrees % T;
  remainder_F = (end_index - begin_index) % F;
  
  for (iIndex = begin_index; iIndex < end_index - remainder_F; iIndex+=F){
    score = 0;
    for (tindex = 0; tindex < nbTrees - remainder; tindex+=T) 
      for (i=0; i<F; i++)
        for (j=0; j<T; j++)
          score += getLeaf(trees[tindex+j], features[iIndex+i])->threshold;
    // deal with remaining trees
    for (i=0; i<F; i++)
      for (j=0; j<remainder; j++)
        score += getLeaf(trees[nbTrees - remainder +j], features[iIndex+i])->threshold;
        if (printScores) 
            printf("%f\n", score);
    sum += score;
  }
  
  //deal with remaining docs
  score = 0;
  for (tindex = 0; tindex < nbTrees - remainder; tindex+=T) 
    for (i=0; i<remainder_F; i++)
      for (j=0; j<T; j++)
        score += getLeaf(trees[tindex+j], features[end_index - remainder_F + i])->threshold;
  // deal with remaining trees
  for (i=0; i<remainder_F; i++)
    for (j=0; j<remainder; j++)
      score += getLeaf(trees[nbTrees - remainder +j], features[end_index - remainder_F + i])->threshold;
  
  sum += score;
  fprintf(fp_log, "Thread %ld's score=%f\n", tid, sum);
  fprintf(fp_log, "Thread %ld done.\n", tid);

  //exit thread and pass back sum
  thread_sum[tid] = sum;
  pthread_exit((void*) t);
}


int main(int argc, char** args) {
  if(!isPresentCL(argc, args, (char*) "-ensemble") ||
     !isPresentCL(argc, args, (char*) "-instances") ||
     !isPresentCL(argc, args, (char*) "-maxLeaves")) {
    return -1;
  }

  char* configFile = getValueCL(argc, args, (char*) "-ensemble");
  char* featureFile = getValueCL(argc, args, (char*) "-instances");
  int maxNumberOfLeaves = atoi(getValueCL(argc, args, (char*) "-maxLeaves"));
  printScores = isPresentCL(argc, args, (char*) "-print");

  // Read ensemble
  FILE *fp = fopen(configFile, "r");
  fscanf(fp, "%d", &nbTrees);

  // Array of pointers to tree roots, one per tree in the ensemble
  trees = (StructPlus**) malloc(nbTrees * sizeof(StructPlus*));
  int tindex = 0;

  // Number of nodes in a tree does not exceed (maxLeaves * 2)
  int maxTreeSize = 2 * maxNumberOfLeaves;
  long treeSize;

  for(tindex = 0; tindex < nbTrees; tindex++) {
    fscanf(fp, "%ld", &treeSize);

    trees[tindex] = createNodes(maxTreeSize);

    char text[20];
    long line = 0;
    fscanf(fp, "%s", text);
    while(strcmp(text, "end") != 0) {
      long id;
      fscanf(fp, "%ld", &id);

      // A "root" node contains a feature id and a threshold
      if(strcmp(text, "root") == 0) {
        int fid;
        float threshold;
        fscanf(fp, "%d %f", &fid, &threshold);
        setRoot(trees[tindex], id, fid, threshold);
      } else if(strcmp(text, "node") == 0) {
        int fid;
        long pid;
        float threshold;
        int leftChild = 0;
        // Read Id of the parent node, feature id, subtree (left or right),
        // and threshold
        fscanf(fp, "%ld %d %d %f", &pid, &fid, &leftChild, &threshold);

        // Find the parent node, based in parent id
        int parentIndex = 0;
        for(parentIndex = 0; parentIndex < maxTreeSize; parentIndex++) {
          if(trees[tindex][parentIndex].id == pid) {
            break;
          }
        }
        // Add the new node
        if(trees[tindex][parentIndex].fid >= 0) {
          addNode(trees[tindex], parentIndex, line, id, leftChild, fid, threshold);
        }
      } else if(strcmp(text, "leaf") == 0) {
        long pid;
        int leftChild = 0;
        float value;
        fscanf(fp, "%ld %d %f", &pid, &leftChild, &value);

        int parentIndex = 0;
        for(parentIndex = 0; parentIndex < maxTreeSize; parentIndex++) {
          if(trees[tindex][parentIndex].id == pid) {
            break;
          }
        }
        if(trees[tindex][parentIndex].fid >= 0) {
          addNode(trees[tindex], parentIndex, line, id, leftChild, 0, value);
        }
      }
      line++;
      fscanf(fp, "%s", text);
    }
    // Re-organize tree memory layout
    trees[tindex] = compress(trees[tindex]);
  }
  fclose(fp);

  // Read instances (SVM Light format
  int numberOfFeatures = 0;
  numberOfInstances = 0;

  fp = fopen(featureFile, "r");
  fscanf(fp, "%d %d", &numberOfInstances, &numberOfFeatures);
  int divisibleNumberOfInstances = numberOfInstances;
  while(divisibleNumberOfInstances % F !=0)
    divisibleNumberOfInstances++;
  features = (float**) malloc(divisibleNumberOfInstances * sizeof(float*));
  int i = 0; int j=0;
  for(i = 0; i < divisibleNumberOfInstances; i++) {
    features[i] = (float*) malloc(numberOfFeatures * sizeof(float));
  }

  float fvalue;
  int fIndex = 0, iIndex = 0;
  int ignore;
  char text[20];
  char comment[1000];
  for(iIndex = 0; iIndex < numberOfInstances; iIndex++) {
    fscanf(fp, "%d %[^:]:%d", &ignore, text, &ignore);
    for(fIndex = 0; fIndex < numberOfFeatures; fIndex++) {
      fscanf(fp, "%[^:]:%f", text, &fvalue);
      features[iIndex][fIndex] = fvalue;
    }
    fscanf(fp, "%[^\n]", comment);
  }
  // Compute scores for instances using the ensemble and
  // measure elapsed time
  float sum = 0; // Dummy value just so gcc wouldn't optimize the loop out
  float score;
  struct timeval start, end;

  // pthread variable:
  pthread_t thread[NUM_THREADS];
  pthread_attr_t attr;
  int rc;
  long t;
  void *status;
  fp_log = fopen("log.txt", "w");
  
  // Initialize and set thread detached attribute
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  
  gettimeofday(&start, NULL);
  for(t=0; t<NUM_THREADS; t++) 
  {
    fprintf(fp_log, "Main: creating thread %ld\n", t);
    rc = pthread_create(&thread[t], &attr, BusyWork, (void *)t);
    if (rc)
    {
      printf("ERROR: return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
  }
  // Free attribute and wait for the other threads
  pthread_attr_destroy(&attr);
  for (t=0; t<NUM_THREADS; t++){
    rc = pthread_join(thread[t], &status);
    if (rc) {
      printf("ERROR: return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
    fprintf(fp_log, "Main: completed join with thread %ld having a status of %ld\n", t, status);
                            
    sum += thread_sum[t];
  }

  gettimeofday(&end, NULL);
  fprintf(fp_log, "Main: program completed. Exiting.\n");
  fclose(fp_log);

  if(printScores)
    printf("%f\n", score);
  sum += score;
  

  printf("Time per instance per tree(ns): %5.2f\n",
         (((end.tv_sec * 1000000 + end.tv_usec) -
           (start.tv_sec * 1000000 + start.tv_usec))*1000/((float) numberOfInstances * nbTrees)));
  printf("Ignore this number: %f\n", sum);

  // Free used memory
  for(tindex = 0; tindex < nbTrees; tindex++) {
    destroyTree(trees[tindex]);
  }
  free(trees);
  for(i = 0; i < numberOfInstances; i++) {
    free(features[i]);
  }
  free(features);
  fclose(fp);
  return 0;
}
