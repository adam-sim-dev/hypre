/******************************************************************************
 * Copyright (c) 1998 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 ******************************************************************************/

#include "_hypre_Euclid.h"
/* #include "SubdomainGraph_dh.h" */
/* #include "getRow_dh.h" */
/* #include "Mem_dh.h" */
/* #include "Parser_dh.h" */
/* #include "Hash_i_dh.h" */
/* #include "mat_dh_private.h" */
/* #include "io_dh.h" */
/* #include "SortedSet_dh.h" */
/* #include "shellSort_dh.h" */

#ifndef WIN32
/* for debugging only! */
#include <unistd.h>
#endif

static void init_seq_private(SubdomainGraph_dh s, HYPRE_Int blocks, bool bj, void *A);
static void init_mpi_private(SubdomainGraph_dh s, HYPRE_Int blocks, bool bj, void *A);
/*
static void partition_metis_private(SubdomainGraph_dh s, void *A);

  grep for same below!
*/
static void allocate_storage_private(SubdomainGraph_dh s, HYPRE_Int blocks, HYPRE_Int m, bool bj);
static void form_subdomaingraph_mpi_private(SubdomainGraph_dh s);
static void form_subdomaingraph_seq_private(SubdomainGraph_dh s, HYPRE_Int m, void *A);
static void find_all_neighbors_sym_private(SubdomainGraph_dh s, HYPRE_Int m, void *A);
static void find_all_neighbors_unsym_private(SubdomainGraph_dh s, HYPRE_Int m, void *A);
static void find_bdry_nodes_sym_private(SubdomainGraph_dh s, HYPRE_Int m, void* A,
                     HYPRE_Int *interiorNodes, HYPRE_Int *bdryNodes,
                     HYPRE_Int *interiorCount, HYPRE_Int *bdryCount);
static void find_bdry_nodes_unsym_private(SubdomainGraph_dh s, HYPRE_Int m, void* A,
                     HYPRE_Int *interiorNodes, HYPRE_Int *bdryNodes,
                     HYPRE_Int *interiorCount, HYPRE_Int *bdryCount);

static void find_bdry_nodes_seq_private(SubdomainGraph_dh s, HYPRE_Int m, void* A);
  /* above also forms n2o[] and o2n[] */

static void find_ordered_neighbors_private(SubdomainGraph_dh s);
static void color_subdomain_graph_private(SubdomainGraph_dh s);
static void adjust_matrix_perms_private(SubdomainGraph_dh s, HYPRE_Int m);

#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhCreate"
void SubdomainGraph_dhCreate(SubdomainGraph_dh *s)
{
  START_FUNC_DH
  struct _subdomain_dh* tmp = (struct _subdomain_dh*)MALLOC_DH(sizeof(struct _subdomain_dh)); CHECK_V_ERROR;
  *s = tmp;

  tmp->blocks = 1;
  tmp->ptrs = tmp->adj = NULL;
  tmp->colors = 1;
  tmp->colorVec = NULL;
  tmp->o2n_sub = tmp->n2o_sub = NULL;
  tmp->beg_row = tmp->beg_rowP = NULL;
  tmp->bdry_count = tmp->row_count = NULL;
  tmp->loNabors = tmp->hiNabors = tmp->allNabors = NULL;
  tmp->loCount = tmp->hiCount = tmp->allCount = 0;

  tmp->m = 0;
  tmp->n2o_row = tmp->o2n_col = NULL;
  tmp->o2n_ext = tmp->n2o_ext = NULL;

  tmp->doNotColor = Parser_dhHasSwitch(parser_dh, "-doNotColor");
  tmp->debug = Parser_dhHasSwitch(parser_dh, "-debug_SubGraph");
  { HYPRE_Int i;
    for (i=0; i<TIMING_BINS_SG; ++i) tmp->timing[i] = 0.0;
  }
  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhDestroy"
void SubdomainGraph_dhDestroy(SubdomainGraph_dh s)
{
  START_FUNC_DH
  if (s->ptrs != NULL) { FREE_DH(s->ptrs); CHECK_V_ERROR; }
  if (s->adj != NULL) { FREE_DH(s->adj); CHECK_V_ERROR; }
  if (s->colorVec != NULL) { FREE_DH(s->colorVec); CHECK_V_ERROR; }
  if (s->o2n_sub != NULL) { FREE_DH(s->o2n_sub); CHECK_V_ERROR; }
  if (s->n2o_sub != NULL) { FREE_DH(s->n2o_sub); CHECK_V_ERROR; }

  if (s->beg_row != NULL) { FREE_DH(s->beg_row); CHECK_V_ERROR; }
  if (s->beg_rowP != NULL) { FREE_DH(s->beg_rowP); CHECK_V_ERROR; }
  if (s->row_count != NULL) { FREE_DH(s->row_count); CHECK_V_ERROR; }
  if (s->bdry_count != NULL) { FREE_DH(s->bdry_count); CHECK_V_ERROR; }
  if (s->loNabors != NULL) { FREE_DH(s->loNabors); CHECK_V_ERROR; }
  if (s->hiNabors != NULL) { FREE_DH(s->hiNabors); CHECK_V_ERROR; }
  if (s->allNabors != NULL) { FREE_DH(s->allNabors); CHECK_V_ERROR; }

  if (s->n2o_row != NULL) { FREE_DH(s->n2o_row); CHECK_V_ERROR; }
  if (s->o2n_col != NULL) { FREE_DH(s->o2n_col); CHECK_V_ERROR; }
  if (s->o2n_ext != NULL) { Hash_i_dhDestroy(s->o2n_ext); CHECK_V_ERROR; }
  if (s->n2o_ext != NULL) { Hash_i_dhDestroy(s->n2o_ext); CHECK_V_ERROR; }
  FREE_DH(s); CHECK_V_ERROR;
  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhInit"
void SubdomainGraph_dhInit(SubdomainGraph_dh s, HYPRE_Int blocks, bool bj, void *A)
{
  START_FUNC_DH
  HYPRE_Real t1 = hypre_MPI_Wtime();

  if (blocks < 1) blocks = 1;

  if (np_dh == 1 || blocks > 1) {
    s->blocks = blocks;
    init_seq_private(s, blocks, bj, A); CHECK_V_ERROR;
  } else {
    s->blocks = np_dh;
    init_mpi_private(s, np_dh, bj, A); CHECK_V_ERROR;
  }

  s->timing[TOTAL_SGT] += (hypre_MPI_Wtime() - t1);
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhFindOwner"
HYPRE_Int SubdomainGraph_dhFindOwner(SubdomainGraph_dh s, HYPRE_Int idx, bool permuted)
{
  START_FUNC_DH
  HYPRE_Int sd;
  HYPRE_Int *beg_row = s->beg_row;
  HYPRE_Int *row_count = s->row_count;
  HYPRE_Int owner = -1, blocks = s->blocks;

  if (permuted) beg_row = s->beg_rowP;

  /* determine the subdomain that contains "idx" */
  for (sd=0; sd<blocks; ++sd) {
    if (idx >= beg_row[sd] && idx < beg_row[sd]+row_count[sd]) {
      owner = sd;
      break;
    }
  }

  if (owner == -1) {

hypre_fprintf(stderr, "@@@ failed to find owner for idx = %i @@@\n", idx);
hypre_fprintf(stderr, "blocks= %i\n", blocks);

    hypre_sprintf(msgBuf_dh, "failed to find owner for idx = %i", idx);
    SET_ERROR(-1, msgBuf_dh);
  }

  END_FUNC_VAL(owner)
}


#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhPrintStatsLong"
void SubdomainGraph_dhPrintStatsLong(SubdomainGraph_dh s, FILE *fp)
{
  START_FUNC_DH
    HYPRE_Int i, j, k;
    HYPRE_Real max = 0, min = (HYPRE_Real) INT_MAX;

    hypre_fprintf(fp, "\n------------- SubdomainGraph_dhPrintStatsLong -----------\n");
    hypre_fprintf(fp, "colors used     = %i\n", s->colors);
    hypre_fprintf(fp, "subdomain count = %i\n", s->blocks);


    hypre_fprintf(fp, "\ninterior/boundary node ratios:\n");

    for (i=0; i<s->blocks; ++i) {
      HYPRE_Int inNodes = s->row_count[i] - s->bdry_count[i];
      HYPRE_Int bdNodes = s->bdry_count[i];
      HYPRE_Real ratio;

      if (bdNodes == 0) {
        ratio = -1;
      } else {
        ratio = (HYPRE_Real)inNodes/(HYPRE_Real)bdNodes;
      }

      max = MAX(max, ratio);
      min = MIN(min, ratio);
      hypre_fprintf(fp, "   P_%i: first= %3i  rowCount= %3i  interior= %3i  bdry= %3i  ratio= %0.1f\n",
                   i, 1+s->beg_row[i], s->row_count[i], inNodes,
                   bdNodes, ratio);
    }


    hypre_fprintf(fp, "\nmax interior/bdry ratio = %.1f\n", max);
    hypre_fprintf(fp, "min interior/bdry ratio = %.1f\n", min);


    /*-----------------------------------------
     * subdomain graph
     *-----------------------------------------*/
    if (s->adj != NULL) {
      hypre_fprintf(fp, "\nunpermuted subdomain graph: \n");
      for (i=0; i<s->blocks; ++i) {
        hypre_fprintf(fp, "%i :: ", i);
        for (j=s->ptrs[i]; j<s->ptrs[i+1]; ++j) {
          hypre_fprintf(fp, "%i  ", s->adj[j]);
        }
        hypre_fprintf(fp, "\n");
      }
    }


    /*-----------------------------------------
     * subdomain permutation
     *-----------------------------------------*/
    hypre_fprintf(fp, "\no2n subdomain permutation:\n");
    for (i=0; i<s->blocks; ++i) {
      hypre_fprintf(fp, "  %i %i\n", i, s->o2n_sub[i]);
    }
    hypre_fprintf(fp, "\n");

   if (np_dh > 1) {

    /*-----------------------------------------
     * local n2o_row permutation
     *-----------------------------------------*/
    hypre_fprintf(fp, "\nlocal n2o_row permutation:\n   ");
    for (i=0; i<s->row_count[myid_dh]; ++i) {
      hypre_fprintf(fp, "%i ", s->n2o_row[i]);
    }
    hypre_fprintf(fp, "\n");

    /*-----------------------------------------
     * local n2o permutation
     *-----------------------------------------*/
    hypre_fprintf(fp, "\nlocal o2n_col permutation:\n   ");
    for (i=0; i<s->row_count[myid_dh]; ++i) {
      hypre_fprintf(fp, "%i ", s->o2n_col[i]);
    }
    hypre_fprintf(fp, "\n");

  } else {
    /*-----------------------------------------
     * local n2o_row permutation
     *-----------------------------------------*/
    hypre_fprintf(fp, "\nlocal n2o_row permutation:\n");
    hypre_fprintf(fp, "--------------------------\n");
    for (k=0; k<s->blocks; ++k) {
      HYPRE_Int beg_row = s->beg_row[k];
      HYPRE_Int end_row = beg_row + s->row_count[k];

      for (i=beg_row; i<end_row; ++i) {
        hypre_fprintf(fp, "%i ", s->n2o_row[i]);
      }
      hypre_fprintf(fp, "\n");
    }

    /*-----------------------------------------
     * local n2o permutation
     *-----------------------------------------*/
    hypre_fprintf(fp, "\nlocal o2n_col permutation:\n");
    hypre_fprintf(fp, "--------------------------\n");
    for (k=0; k<s->blocks; ++k) {
      HYPRE_Int beg_row = s->beg_row[k];
      HYPRE_Int end_row = beg_row + s->row_count[k];

      for (i=beg_row; i<end_row; ++i) {
        hypre_fprintf(fp, "%i ", s->o2n_col[i]);
      }
      hypre_fprintf(fp, "\n");
    }


  }

  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "init_seq_private"
void init_seq_private(SubdomainGraph_dh s, HYPRE_Int blocks, bool bj, void *A)
{
  START_FUNC_DH
  HYPRE_Int m, n, beg_row;
  HYPRE_Real t1;

  /*-------------------------------------------------------
   * get number of local rows (m), global rows (n), and
   * global numbering of first locally owned row
   * (for sequential, beg_row=0 and m == n
   *-------------------------------------------------------*/
  EuclidGetDimensions(A, &beg_row, &m, &n); CHECK_V_ERROR;
  s->m = n;

  /*-------------------------------------------------------
   * allocate storage for all data structures
   * EXCEPT s->adj and hash tables.
   * (but note that hash tables aren't used for sequential)
   *-------------------------------------------------------*/
  allocate_storage_private(s,blocks,m, bj); CHECK_V_ERROR;

  /*-------------------------------------------------------------
   * Fill in: beg_row[]
   *          beg_rowP[]
   *          row_count[]
   * At this point, beg_rowP[] is a copy of beg_row[])
   *-------------------------------------------------------------*/
  { HYPRE_Int i;
    HYPRE_Int rpp = m/blocks;

    if (rpp*blocks < m) ++rpp;

    s->beg_row[0] = 0;
    for (i=1; i<blocks; ++i) s->beg_row[i] = rpp + s->beg_row[i-1];
    for (i=0; i<blocks; ++i) s->row_count[i] = rpp;
    s->row_count[blocks-1] = m - rpp*(blocks-1);
  }
  hypre_TMemcpy(s->beg_rowP,  s->beg_row, HYPRE_Int, blocks, HYPRE_MEMORY_HOST, HYPRE_MEMORY_HOST);


  /*-----------------------------------------------------------------
   * Find all neighboring processors in subdomain graph.
   * This block fills in: allNabors[]
   *-----------------------------------------------------------------*/
   /* NA for sequential! */


  /*-------------------------------------------------------
   * Count number of interior nodes for each subdomain;
   * also, form permutation vector to order boundary
   * nodes last in each subdomain.
   * This block fills in: bdry_count[]
   *                      n2o_col[]
   *                      o2n_row[]
   *-------------------------------------------------------*/
  t1 = hypre_MPI_Wtime();
  if (!bj) {
    find_bdry_nodes_seq_private(s, m, A); CHECK_V_ERROR;
  }
  else {
    HYPRE_Int i;
    for (i=0; i<m; ++i) {
      s->n2o_row[i] = i;
      s->o2n_col[i] = i;
    }
  }
  s->timing[ORDER_BDRY_SGT] += (hypre_MPI_Wtime() - t1);

  /*-------------------------------------------------------
   * Form subdomain graph,
   * then color and reorder subdomain graph.
   * This block fills in: ptr[]
   *                      adj[]
   *                      o2n_sub[]
   *                      n2o_sub[]
   *                      beg_rowP[]
   *-------------------------------------------------------*/
  t1 = hypre_MPI_Wtime();
  if (! bj) {
    form_subdomaingraph_seq_private(s, m, A); CHECK_V_ERROR;
    if (s->doNotColor) {
      HYPRE_Int i;
      printf_dh("subdomain coloring and reordering is OFF\n");
      for (i=0; i<blocks; ++i) {
        s->o2n_sub[i] = i;
        s->n2o_sub[i] = i;
        s->colorVec[i] = 0;
      }
    } else {
      SET_INFO("subdomain coloring and reordering is ON");
      color_subdomain_graph_private(s); CHECK_V_ERROR;
    }
  }

  /* bj setup */
  else {
    HYPRE_Int i;
    for (i=0; i<blocks; ++i) {
      s->o2n_sub[i] = i;
      s->n2o_sub[i] = i;
    }
  }
  s->timing[FORM_GRAPH_SGT] += (hypre_MPI_Wtime() - t1);

  /*-------------------------------------------------------
   * Here's a step we DON'T do for the parallel case:
   * we need to adjust the matrix row and column perms
   * to reflect subdomain reordering (for the parallel
   * case, these permutation vectors are purely local and
   * zero-based)
   *-------------------------------------------------------*/
  if (!bj) {
    adjust_matrix_perms_private(s, m); CHECK_V_ERROR;
  }

  /*-------------------------------------------------------
   * Build lists of lower and higher ordered neighbors.
   * This block fills in: loNabors[]
   *                      hiNabors[]
   *-------------------------------------------------------*/
   /* NA for sequential */

  /*-------------------------------------------------------
   *  Exchange boundary node permutation information with
   *  neighboring processors in the subdomain graph.
   *  This block fills in: o2n_ext (hash table)
   *                       n2o_ext (hash table)
   *-------------------------------------------------------*/
   /* NA for sequential */


  END_FUNC_DH
}


#if 0
#undef __FUNC__
#define __FUNC__ "partition_metis_private"
void partition_metis_private(SubdomainGraph_dh s, void *A)
{
  START_FUNC_DH
  if (ignoreMe) SET_V_ERROR("not implemented");
  END_FUNC_DH
}
#endif

#undef __FUNC__
#define __FUNC__ "allocate_storage_private"
void allocate_storage_private(SubdomainGraph_dh s, HYPRE_Int blocks, HYPRE_Int m, bool bj)
{
  START_FUNC_DH

  if (!bj) {
    s->ptrs = (HYPRE_Int*)MALLOC_DH((blocks+1)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
    s->ptrs[0] = 0;
    s->colorVec = (HYPRE_Int*)MALLOC_DH(blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;
    s->loNabors = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
    s->hiNabors = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
    s->allNabors = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  }

  s->n2o_row = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->o2n_col = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;

  /* these are probably only needed for single mpi task -- ?? */
  /* nope; beg_row and row_ct are needed by ilu_mpi_bj; yuck! */
  s->beg_row = (HYPRE_Int*)MALLOC_DH((blocks)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->beg_rowP = (HYPRE_Int*)MALLOC_DH((blocks)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->row_count = (HYPRE_Int*)MALLOC_DH(blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->bdry_count = (HYPRE_Int*)MALLOC_DH(blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->o2n_sub = (HYPRE_Int*)MALLOC_DH(blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  s->n2o_sub = (HYPRE_Int*)MALLOC_DH(blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;

  END_FUNC_DH
}

/*-----------------------------------------------------------------*/


#undef __FUNC__
#define __FUNC__ "init_mpi_private"
void init_mpi_private(SubdomainGraph_dh s, HYPRE_Int blocks, bool bj, void *A)
{
  START_FUNC_DH
  HYPRE_Int m, n, beg_row;
  bool symmetric;
  HYPRE_Real t1;

  symmetric = Parser_dhHasSwitch(parser_dh, "-sym"); CHECK_V_ERROR;
  if (Parser_dhHasSwitch(parser_dh, "-makeSymmetric")) {
    symmetric = true;
  }

  /*-------------------------------------------------------
   * get number of local rows (m), global rows (n), and
   * global numbering of first locally owned row
   *-------------------------------------------------------*/
  EuclidGetDimensions(A, &beg_row, &m, &n); CHECK_V_ERROR;
  s->m = m;


  /*-------------------------------------------------------
   * allocate storage for all data structures
   * EXCEPT s->adj and hash tables.
   *-------------------------------------------------------*/
  allocate_storage_private(s, blocks, m, bj); CHECK_V_ERROR;

  /*-------------------------------------------------------------
   * Fill in: beg_row[]
   *          beg_rowP[]
   *          row_count[]
   * At this point, beg_rowP[] is a copy of beg_row[])
   *-------------------------------------------------------------*/
  if (!bj) {
    hypre_MPI_Allgather(&beg_row, 1, HYPRE_MPI_INT, s->beg_row, 1, HYPRE_MPI_INT, comm_dh);
    hypre_MPI_Allgather(&m, 1, HYPRE_MPI_INT, s->row_count, 1, HYPRE_MPI_INT, comm_dh);
    hypre_TMemcpy(s->beg_rowP,  s->beg_row, HYPRE_Int, np_dh, HYPRE_MEMORY_HOST, HYPRE_MEMORY_HOST);
  } else {
    s->beg_row[myid_dh] = beg_row;
    s->beg_rowP[myid_dh] = beg_row;
    s->row_count[myid_dh] = m;
  }

  /*-----------------------------------------------------------------
   * Find all neighboring processors in subdomain graph.
   * This block fills in: allNabors[]
   *-----------------------------------------------------------------*/
  if (! bj) {
    t1 = hypre_MPI_Wtime();
    if (symmetric) {
      find_all_neighbors_sym_private(s, m, A); CHECK_V_ERROR;
    } else {
      find_all_neighbors_unsym_private(s, m, A); CHECK_V_ERROR;
    }
    s->timing[FIND_NABORS_SGT] += (hypre_MPI_Wtime() - t1);
  }


  /*-----------------------------------------------------------------
   *  determine which rows are boundary rows, and which are interior
   *  rows; also, form permutation vector to order interior
   *  nodes first within each subdomain
   *  This block fills in: bdry_count[]
   *                       n2o_col[]
   *                       o2n_row[]
   *-----------------------------------------------------------------*/
  t1 = hypre_MPI_Wtime();
  if (!bj) {
      HYPRE_Int *interiorNodes, *bdryNodes;
      HYPRE_Int interiorCount = 0, bdryCount;
      HYPRE_Int *o2n = s->o2n_col, idx;
      HYPRE_Int i;

      interiorNodes = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;
      bdryNodes     = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;

      /* divide this subdomain's rows into interior and boundary rows;
         the returned lists are with respect to local numbering.
      */
      if (symmetric) {
        find_bdry_nodes_sym_private(s, m, A,
             interiorNodes, bdryNodes, &interiorCount, &bdryCount); CHECK_V_ERROR;
      } else {
        find_bdry_nodes_unsym_private(s, m, A,
             interiorNodes, bdryNodes, &interiorCount, &bdryCount); CHECK_V_ERROR;
      }

      /* exchange number of boundary rows with all neighbors */
      hypre_MPI_Allgather(&bdryCount, 1, HYPRE_MPI_INT, s->bdry_count, 1, HYPRE_MPI_INT, comm_dh);

      /* form local permutation */
      idx = 0;
      for (i=0; i<interiorCount; ++i) {
        o2n[interiorNodes[i]] = idx++;
      }
      for (i=0; i<bdryCount; ++i) {
        o2n[bdryNodes[i]] = idx++;
      }

      /* invert permutation */
      invert_perm(m, o2n, s->n2o_row); CHECK_V_ERROR;

      FREE_DH(interiorNodes); CHECK_V_ERROR;
      FREE_DH(bdryNodes); CHECK_V_ERROR;
  }

  /* bj setup */
  else {
    HYPRE_Int *o2n = s->o2n_col, *n2o = s->n2o_row;
    HYPRE_Int i, m = s->m;

    for (i=0; i<m; ++i) {
      o2n[i] = i;
      n2o[i] = i;
    }
  }
  s->timing[ORDER_BDRY_SGT] += (hypre_MPI_Wtime() - t1);

  /*-------------------------------------------------------
   * Form subdomain graph,
   * then color and reorder subdomain graph.
   * This block fills in: ptr[]
   *                      adj[]
   *                      o2n_sub[]
   *                      n2o_sub[]
   *                      beg_rowP[]
   *-------------------------------------------------------*/
  if (!bj) {
    t1 = hypre_MPI_Wtime();
    form_subdomaingraph_mpi_private(s); CHECK_V_ERROR;
    if (s->doNotColor) {
      HYPRE_Int i;
      printf_dh("subdomain coloring and reordering is OFF\n");
      for (i=0; i<blocks; ++i) {
        s->o2n_sub[i] = i;
        s->n2o_sub[i] = i;
        s->colorVec[i] = 0;
      }
    } else {
      SET_INFO("subdomain coloring and reordering is ON");
      color_subdomain_graph_private(s); CHECK_V_ERROR;
    }
    s->timing[FORM_GRAPH_SGT] += (hypre_MPI_Wtime() - t1);
  }

  /*-------------------------------------------------------
   * Build lists of lower and higher ordered neighbors.
   * This block fills in: loNabors[]
   *                      hiNabors[]
   *-------------------------------------------------------*/
  if (!bj) {
    find_ordered_neighbors_private(s); CHECK_V_ERROR;
  }

  /*-------------------------------------------------------
   *  Exchange boundary node permutation information with
   *  neighboring processors in the subdomain graph.
   *  This block fills in: o2n_ext (hash table)
   *                       n2o_ext (hash table)
   *-------------------------------------------------------*/
  if (!bj) {
    t1 = hypre_MPI_Wtime();
    SubdomainGraph_dhExchangePerms(s); CHECK_V_ERROR;
    s->timing[EXCHANGE_PERMS_SGT] += (hypre_MPI_Wtime() - t1);
  }

  END_FUNC_DH
}



#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhExchangePerms"
void SubdomainGraph_dhExchangePerms(SubdomainGraph_dh s)
{
  START_FUNC_DH
  hypre_MPI_Request *recv_req = NULL, *send_req = NULL;
  hypre_MPI_Status *status = NULL;
  HYPRE_Int *nabors = s->allNabors, naborCount = s->allCount;
  HYPRE_Int i, j, *sendBuf = NULL, *recvBuf = NULL, *naborIdx = NULL, nz;
  HYPRE_Int m = s->row_count[myid_dh];
  HYPRE_Int beg_row = s->beg_row[myid_dh];
  HYPRE_Int beg_rowP = s->beg_rowP[myid_dh];
  HYPRE_Int *bdryNodeCounts = s->bdry_count;
  HYPRE_Int myBdryCount = s->bdry_count[myid_dh];
  bool debug = false;
  HYPRE_Int myFirstBdry = m - myBdryCount;
  HYPRE_Int *n2o_row = s->n2o_row;
  Hash_i_dh n2o_table, o2n_table;

  if (logFile != NULL && s->debug) debug = true;

  /* allocate send buffer, and copy permutation info to buffer;
     each entry is a <old_value, new_value> pair.
   */
  sendBuf = (HYPRE_Int*)MALLOC_DH(2*myBdryCount*sizeof(HYPRE_Int)); CHECK_V_ERROR;


  if (debug) {
    hypre_fprintf(logFile, "\nSUBG myFirstBdry= %i  myBdryCount= %i  m= %i  beg_rowP= %i\n", 1+ myFirstBdry, myBdryCount, m, 1+beg_rowP);
    fflush(logFile);
  }

  for (i=myFirstBdry, j=0; j<myBdryCount; ++i, ++j) {
    sendBuf[2*j] = n2o_row[i]+beg_row;
    sendBuf[2*j+1] = i+beg_rowP;
  }

  if (debug) {
    hypre_fprintf(logFile, "\nSUBG SEND_BUF:\n");
    for (i=myFirstBdry, j=0; j<myBdryCount; ++i, ++j) {
      hypre_fprintf(logFile, "SUBG  %i, %i\n", 1+sendBuf[2*j], 1+sendBuf[2*j+1]);
    }
    fflush(logFile);
  }

  /* allocate a receive buffer for each nabor in the subdomain graph,
     and set up index array for locating the beginning of each
     nabor's buffers.
   */
  naborIdx = (HYPRE_Int*)MALLOC_DH((1+naborCount)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  naborIdx[0] = 0;
  nz = 0;
  for (i=0; i<naborCount; ++i) {
    nz += (2*bdryNodeCounts[nabors[i]]);
    naborIdx[i+1] = nz;
  }


  recvBuf = (HYPRE_Int*)MALLOC_DH(nz*sizeof(HYPRE_Int)); CHECK_V_ERROR;


/* for (i=0; i<nz; ++i) recvBuf[i] = -10; */

  /* perform sends and receives */
  recv_req = (hypre_MPI_Request*)MALLOC_DH(naborCount*sizeof(hypre_MPI_Request)); CHECK_V_ERROR;
  send_req = (hypre_MPI_Request*)MALLOC_DH(naborCount*sizeof(hypre_MPI_Request)); CHECK_V_ERROR;
  status = (hypre_MPI_Status*)MALLOC_DH(naborCount*sizeof(hypre_MPI_Status)); CHECK_V_ERROR;

  for (i=0; i<naborCount; ++i) {
    HYPRE_Int nabr = nabors[i];
    HYPRE_Int *buf = recvBuf + naborIdx[i];
    HYPRE_Int ct = 2*bdryNodeCounts[nabr];


    hypre_MPI_Isend(sendBuf, 2*myBdryCount, HYPRE_MPI_INT, nabr, 444, comm_dh, &(send_req[i]));

    if (debug) {
      hypre_fprintf(logFile , "SUBG   sending %i elts to %i\n", 2*myBdryCount, nabr);
      fflush(logFile);
    }

    hypre_MPI_Irecv(buf, ct, HYPRE_MPI_INT, nabr, 444, comm_dh, &(recv_req[i]));

    if (debug) {
      hypre_fprintf(logFile, "SUBG  receiving %i elts from %i\n", ct, nabr);
      fflush(logFile);
    }
  }

  hypre_MPI_Waitall(naborCount, send_req, status);
  hypre_MPI_Waitall(naborCount, recv_req, status);

  Hash_i_dhCreate(&n2o_table, nz/2); CHECK_V_ERROR;
  Hash_i_dhCreate(&o2n_table, nz/2); CHECK_V_ERROR;
  s->n2o_ext = n2o_table;
  s->o2n_ext = o2n_table;

  /* insert non-local boundary node permutations in lookup tables */
  for (i=0; i<nz; i += 2) {
    HYPRE_Int old = recvBuf[i];
    HYPRE_Int newV = recvBuf[i+1];

    if (debug) {
      hypre_fprintf(logFile, "SUBG  i= %i  old= %i  newV= %i\n", i, old+1, newV+1);
      fflush(logFile);
    }

    Hash_i_dhInsert(o2n_table, old, newV); CHECK_V_ERROR;
    Hash_i_dhInsert(n2o_table, newV, old); CHECK_V_ERROR;
  }


  if (recvBuf != NULL) { FREE_DH(recvBuf); CHECK_V_ERROR; }
  if (naborIdx != NULL) { FREE_DH(naborIdx); CHECK_V_ERROR; }
  if (sendBuf != NULL) { FREE_DH(sendBuf); CHECK_V_ERROR; }
  if (recv_req != NULL) { FREE_DH(recv_req); CHECK_V_ERROR; }
  if (send_req != NULL) { FREE_DH(send_req); CHECK_V_ERROR; }
  if (status != NULL) { FREE_DH(status); CHECK_V_ERROR; }

  END_FUNC_DH
}



#undef __FUNC__
#define __FUNC__ "form_subdomaingraph_mpi_private"
void form_subdomaingraph_mpi_private(SubdomainGraph_dh s)
{
  START_FUNC_DH
  HYPRE_Int *nabors = s->allNabors, nct = s->allCount;
  HYPRE_Int *idxAll = NULL;
  HYPRE_Int i, j, nz, *adj, *ptrs = s->ptrs;
  hypre_MPI_Request *recvReqs = NULL, sendReq;
  hypre_MPI_Status *statuses = NULL, status;

  /* all processors tell root how many nabors they have */
  if (myid_dh == 0) {
    idxAll = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  }
  hypre_MPI_Gather(&nct, 1, HYPRE_MPI_INT, idxAll, 1, HYPRE_MPI_INT, 0, comm_dh);

  /* root counts edges in graph, and broacasts to all */
  if (myid_dh == 0) {
    nz = 0;
    for (i=0; i<np_dh; ++i) nz += idxAll[i];
  }
  hypre_MPI_Bcast(&nz, 1, HYPRE_MPI_INT, 0, comm_dh);

  /* allocate space for adjacency lists (memory for the
     pointer array was previously allocated)
   */
  adj = s->adj = (HYPRE_Int*)MALLOC_DH(nz*sizeof(HYPRE_Int)); CHECK_V_ERROR;

  /* root receives adjacency lists from all processors */
  if (myid_dh == 0) {
    recvReqs = (hypre_MPI_Request*)MALLOC_DH(np_dh*sizeof(hypre_MPI_Request)); CHECK_V_ERROR;
    statuses = (hypre_MPI_Status*)MALLOC_DH(np_dh*sizeof(hypre_MPI_Status)); CHECK_V_ERROR;

    /* first, set up row pointer array */
    ptrs[0] = 0;
    for (j=0; j<np_dh; ++j) ptrs[j+1] = ptrs[j] + idxAll[j];

    /* second, start the receives */
    for (j=0; j<np_dh; ++j) {
      HYPRE_Int ct = idxAll[j];

      hypre_MPI_Irecv(adj+ptrs[j], ct, HYPRE_MPI_INT, j, 42, comm_dh, recvReqs+j);
    }
  }

  /* all processors send root their adjacency list */
  hypre_MPI_Isend(nabors, nct, HYPRE_MPI_INT, 0, 42, comm_dh, &sendReq);

  /* wait for comms to go through */
  if (myid_dh == 0) {
    hypre_MPI_Waitall(np_dh, recvReqs, statuses);
  }
  hypre_MPI_Wait(&sendReq, &status);

  /* root broadcasts assembled subdomain graph to all processors */
  hypre_MPI_Bcast(ptrs, 1+np_dh, HYPRE_MPI_INT, 0, comm_dh);
  hypre_MPI_Bcast(adj, nz, HYPRE_MPI_INT, 0, comm_dh);

  if (idxAll != NULL) { FREE_DH(idxAll); CHECK_V_ERROR; }
  if (recvReqs != NULL) { FREE_DH(recvReqs); CHECK_V_ERROR; }
  if (statuses != NULL) { FREE_DH(statuses); CHECK_V_ERROR; }

  END_FUNC_DH
}

/* this is ugly and inefficient; but seq mode is primarily
   for debugging and testing, so there.
*/
#undef __FUNC__
#define __FUNC__ "form_subdomaingraph_seq_private"
void form_subdomaingraph_seq_private(SubdomainGraph_dh s, HYPRE_Int m, void *A)
{
  HYPRE_UNUSED_VAR(m);

  START_FUNC_DH
  HYPRE_Int *dense, i, j, row, blocks = s->blocks;
  HYPRE_Int *cval, len, *adj;
  HYPRE_Int idx = 0, *ptrs = s->ptrs;

  /* allocate storage for adj[]; since this function is intended
     for debugging/testing, and the number of blocks should be
     relatively small, we'll punt and allocate the maximum
     possibly needed.
  */
  adj = s->adj = (HYPRE_Int*)MALLOC_DH(blocks*blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;

  dense = (HYPRE_Int*)MALLOC_DH(blocks*blocks*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  for (i=0; i<blocks*blocks; ++i) dense[i] = 0;

  /* loop over each block's rows to identify all boundary nodes */
  for (i=0; i<blocks; ++i) {
    HYPRE_Int beg_row = s->beg_row[i];
    HYPRE_Int end_row = beg_row + s->row_count[i];

    for (row=beg_row; row<end_row; ++row) {
      EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
      for (j=0; j<len; ++j) {
        HYPRE_Int col = cval[j];
        if (col < beg_row  ||  col >= end_row) {
          HYPRE_Int owner = SubdomainGraph_dhFindOwner(s, col, false); CHECK_V_ERROR;
          dense[i*blocks+owner] = 1;
          dense[owner*blocks+i] = 1;
        }
      }
      EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
    }
  }

  /* form sparse csr representation of subdomain graph
     from dense representation
   */
  ptrs[0] = 0;
  for (i=0; i<blocks; ++i) {
    for (j=0; j<blocks; ++j) {
      if (dense[i*blocks+j]) {
        adj[idx++] = j;
      }
    }
    ptrs[i+1] = idx;
  }

  FREE_DH(dense); CHECK_V_ERROR;
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "find_all_neighbors_sym_private"
void find_all_neighbors_sym_private(SubdomainGraph_dh s, HYPRE_Int m, void *A)
{
  START_FUNC_DH
  HYPRE_Int *marker, i, j, beg_row, end_row;
  HYPRE_Int row, len, *cval, ct = 0;
  HYPRE_Int *nabors = s->allNabors;

  marker = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  for (i=0; i<m; ++i) marker[i] = 0;

  SET_INFO("finding nabors in subdomain graph for structurally symmetric matrix");
  SET_INFO("(if this isn't what you want, use '-sym 0' switch)");

  beg_row = s->beg_row[myid_dh];
  end_row = beg_row + s->row_count[myid_dh];

  for (row=beg_row; row<end_row; ++row) {
    EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
    for (j=0; j<len; ++j) {
      HYPRE_Int col = cval[j];
      if (col < beg_row  ||  col >= end_row) {
        HYPRE_Int owner = SubdomainGraph_dhFindOwner(s, col, false); CHECK_V_ERROR;
        if (! marker[owner]) {
          marker[owner] = 1;
          nabors[ct++] = owner;
        }
      }
    }
    EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
  }
  s->allCount = ct;

/* hypre_fprintf(logFile, "@@@@@ allCount= %i\n", ct); */

  if (marker != NULL) { FREE_DH(marker); CHECK_V_ERROR; }
  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "find_all_neighbors_unsym_private"
void find_all_neighbors_unsym_private(SubdomainGraph_dh s, HYPRE_Int m, void *A)
{
  HYPRE_UNUSED_VAR(m);

  START_FUNC_DH
  HYPRE_Int i, j, row, beg_row, end_row;
  HYPRE_Int *marker;
  HYPRE_Int *cval, len, idx = 0;
  HYPRE_Int nz, *nabors = s->allNabors, *myNabors;

  myNabors = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  marker = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  for (i=0; i<np_dh; ++i) marker[i] = 0;

  SET_INFO("finding nabors in subdomain graph for structurally unsymmetric matrix");

  /* loop over this block's boundary rows, finding all nabors in
     subdomain graph
   */
  beg_row = s->beg_row[myid_dh];
  end_row = beg_row + s->row_count[myid_dh];



  /*for each locally owned row ...   */
  for (row=beg_row; row<end_row; ++row) {
    EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
    for (j=0; j<len; ++j) {
      HYPRE_Int col = cval[j];
      /*for each column that corresponds to a non-locally owned row ...  */
      if (col < beg_row  ||  col >= end_row) {
        HYPRE_Int owner = SubdomainGraph_dhFindOwner(s, col, false); CHECK_V_ERROR;
        /*if I've not yet done so ...   */
        if (! marker[owner]) {
          marker[owner] = 1;
          /*append the non-local row's owner in to the list of my nabors
            in the subdomain graph     */
          myNabors[idx++] = owner;
        }
      }
    }
    EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
  }

  /*
  at this point, idx = the number of my neighbors in the subdomain
  graph; equivalently, idx is the number of meaningfull slots in
  the myNabors array.  -dah 1/31/06
  */

  /*
  at this point: marker[j] = 0 indicates that processor j is NOT my nabor
                 marker[j] = 1 indicates that processor j IS my nabor
  however, there may be some nabors that can't be discovered in the above loop
  "//for each locally owned row;" this can happen if the matrix is
  structurally unsymmetric.
  -dah 1/31/06
  */

/* hypre_fprintf(stderr, "[%i] marker: ", myid_dh);
for (j=0; j<np_dh; j++) {
  hypre_fprintf(stderr, "[%i] (j=%d) %d\n", myid_dh, j,  marker[j]);
}
hypre_fprintf(stderr, "\n");
*/

  /* find out who my neighbors are that I cannot discern locally */
  hypre_MPI_Alltoall(marker, 1, HYPRE_MPI_INT, nabors, 1, HYPRE_MPI_INT, comm_dh); CHECK_V_ERROR;

  /* add in neighbors that I know about from scanning my adjacency lists */
  for (i=0; i<idx; ++i) nabors[myNabors[i]] = 1;

  /* remove self from the adjacency list */
  nabors[myid_dh] = 0;

  /*
  at this point: marker[j] = 0 indicates that processor j is NOT my nabor
                 marker[j] = 1 indicates that processor j IS my nabor
  and this is guaranteed to be complete.
  */

  /* form final list of neighboring processors */
  nz = 0;
  for (i=0; i<np_dh; ++i) {
    if (nabors[i]) myNabors[nz++] = i;
  }
  s->allCount = nz;
  hypre_TMemcpy(nabors,  myNabors, HYPRE_Int, nz, HYPRE_MEMORY_HOST, HYPRE_MEMORY_HOST);

  if (marker != NULL) { FREE_DH(marker); CHECK_V_ERROR; }
  if (myNabors != NULL) { FREE_DH(myNabors); CHECK_V_ERROR; }
  END_FUNC_DH
}

/*================================================================*/

#undef __FUNC__
#define __FUNC__ "find_bdry_nodes_sym_private"
void find_bdry_nodes_sym_private(SubdomainGraph_dh s, HYPRE_Int m, void* A,
                     HYPRE_Int *interiorNodes, HYPRE_Int *bdryNodes,
                     HYPRE_Int *interiorCount, HYPRE_Int *bdryCount)
{
  HYPRE_UNUSED_VAR(m);

  START_FUNC_DH
  HYPRE_Int beg_row = s->beg_row[myid_dh];
  HYPRE_Int end_row = beg_row + s->row_count[myid_dh];
  HYPRE_Int row, inCt = 0, bdCt = 0;

  HYPRE_Int j;
  HYPRE_Int *cval;

  /* determine if the row is a boundary row */
  for (row=beg_row; row<end_row; ++row) { /* for each row in the subdomain */
    bool isBdry = false;
    HYPRE_Int len;
    EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;

    for (j=0; j<len; ++j) { /* for each column in the row */
      HYPRE_Int col = cval[j];
      if (col < beg_row  ||  col >= end_row) {
        isBdry = true;
        break;
      }
    }
    EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;

    if (isBdry) {
      bdryNodes[bdCt++] = row-beg_row;
    } else {
      interiorNodes[inCt++] = row-beg_row;
    }
  }

  *interiorCount = inCt;
  *bdryCount = bdCt;

  END_FUNC_DH
}

#define BDRY_NODE_TAG 42

#undef __FUNC__
#define __FUNC__ "find_bdry_nodes_unsym_private"
void find_bdry_nodes_unsym_private(SubdomainGraph_dh s, HYPRE_Int m, void* A,
                     HYPRE_Int *interiorNodes, HYPRE_Int *boundaryNodes,
                     HYPRE_Int *interiorCount, HYPRE_Int *bdryCount)
{
  START_FUNC_DH
  HYPRE_Int beg_row = s->beg_row[myid_dh];
  HYPRE_Int end_row = beg_row + s->row_count[myid_dh];
  HYPRE_Int i, j, row, max;
  HYPRE_Int *cval;
  HYPRE_Int *list, count;
  HYPRE_Int *rpIN = NULL, *rpOUT = NULL;
  HYPRE_Int *sendBuf, *recvBuf;
  HYPRE_Int *marker, inCt, bdCt;
  HYPRE_Int *bdryNodes, nz;
  HYPRE_Int sendCt, recvCt;
  hypre_MPI_Request *sendReq, *recvReq;
  hypre_MPI_Status *status;
  SortedSet_dh ss;

  SortedSet_dhCreate(&ss, m); CHECK_V_ERROR;

  /*-----------------------------------------------------
   * identify all boundary nodes possible using locally
   * owned adjacency lists
   *-----------------------------------------------------*/
  for (row=beg_row; row<end_row; ++row) {
    bool isBdry = false;
    HYPRE_Int len;
    EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;

    for (j=0; j<len; ++j) {
      HYPRE_Int col = cval[j];
      if (col < beg_row  ||  col >= end_row) {
        isBdry = true;           /* this row is a boundary node */
        SortedSet_dhInsert(ss, col); CHECK_V_ERROR;
                                 /* the row "col" is also a boundary node */
      }
    }
    EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;

    if (isBdry) {
      SortedSet_dhInsert(ss, row); CHECK_V_ERROR;
    }
  }

  /*-----------------------------------------------------
   * scan the sorted list to determine what boundary
   * node information to send to whom
   *-----------------------------------------------------*/
  sendBuf = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  recvBuf = (HYPRE_Int*)MALLOC_DH(np_dh*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  rpOUT = (HYPRE_Int*)MALLOC_DH((np_dh+1)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  rpOUT[0] = 0;
  for (i=0; i<np_dh; ++i) sendBuf[i] = 0;

  sendCt = 0; /* total number of processor to whom we will send lists */
  SortedSet_dhGetList(ss, &list, &count); CHECK_V_ERROR;

  for (i=0; i<count; /* i is set below */) {
    HYPRE_Int node = list[i];
    HYPRE_Int owner;
    HYPRE_Int last;

    owner = SubdomainGraph_dhFindOwner(s, node, false); CHECK_V_ERROR;
    last = s->beg_row[owner] + s->row_count[owner];

    /* determine the other boundary nodes that belong to owner */
    while ( (i < count)  && (list[i] < last) ) ++i;
    ++sendCt;
    rpOUT[sendCt] = i;
    sendBuf[owner] = rpOUT[sendCt]-rpOUT[sendCt-1];

  }

  /*-----------------------------------------------------
   * processors tell each other how much information
   * each will send to whom
   *-----------------------------------------------------*/
  hypre_MPI_Alltoall(sendBuf, 1, HYPRE_MPI_INT, recvBuf, 1, HYPRE_MPI_INT, comm_dh); CHECK_V_ERROR;

  /*-----------------------------------------------------
   * exchange boundary node information
   * (note that we also exchange information with ourself!)
   *-----------------------------------------------------*/

  /* first, set up data structures to hold incoming information */
  rpIN = (HYPRE_Int*)MALLOC_DH((np_dh+1)*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  rpIN[0] = 0;
  nz = 0;
  recvCt = 0;
  for (i=0; i<np_dh; ++i) {
    if (recvBuf[i]) {
      ++recvCt;
      nz += recvBuf[i];
      rpIN[recvCt] = nz;
    }
  }
  bdryNodes = (HYPRE_Int*)MALLOC_DH(nz*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  sendReq = (hypre_MPI_Request*)MALLOC_DH(sendCt*sizeof(hypre_MPI_Request)); CHECK_V_ERROR;
  recvReq = (hypre_MPI_Request*)MALLOC_DH(recvCt*sizeof(hypre_MPI_Request)); CHECK_V_ERROR;
  max = MAX(sendCt, recvCt);
  status = (hypre_MPI_Status*)MALLOC_DH(max*sizeof(hypre_MPI_Status)); CHECK_V_ERROR;

  /* second, start receives for incoming data */
  j = 0;
  for (i=0; i<np_dh; ++i) {
    if (recvBuf[i]) {
      hypre_MPI_Irecv(bdryNodes+rpIN[j], recvBuf[i], HYPRE_MPI_INT,
                i, BDRY_NODE_TAG, comm_dh, recvReq+j);
      ++j;
    }
  }

  /* third, start sends for outgoing data */
  j = 0;
  for (i=0; i<np_dh; ++i) {
    if (sendBuf[i]) {
      hypre_MPI_Isend(list+rpOUT[j], sendBuf[i], HYPRE_MPI_INT,
                i, BDRY_NODE_TAG, comm_dh, sendReq+j);
      ++j;
    }
  }

  /* fourth, wait for all comms to finish */
  hypre_MPI_Waitall(sendCt, sendReq, status);
  hypre_MPI_Waitall(recvCt, recvReq, status);

  /* fifth, convert from global to local indices */
  for (i=0; i<nz; ++i) bdryNodes[i] -= beg_row;

  /*-----------------------------------------------------
   * consolidate information from all processors to
   * identify all local boundary nodes
   *-----------------------------------------------------*/
  marker = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;
  for (i=0; i<m; ++i) marker[i] = 0;
  for (i=0; i<nz; ++i) marker[bdryNodes[i]] = 1;

  inCt = bdCt = 0;
  for (i=0; i<m; ++i) {
    if (marker[i]) {
      boundaryNodes[bdCt++] = i;
    } else {
      interiorNodes[inCt++] = i;
    }
  }
  *interiorCount = inCt;
  *bdryCount = bdCt;

  /*-----------------------------------------------------
   * clean up
   *-----------------------------------------------------*/
  SortedSet_dhDestroy(ss); CHECK_V_ERROR;
  if (rpIN != NULL) { FREE_DH(rpIN); CHECK_V_ERROR; }
  if (rpOUT != NULL) { FREE_DH(rpOUT); CHECK_V_ERROR; }
  if (sendBuf != NULL) { FREE_DH(sendBuf); CHECK_V_ERROR; }
  if (recvBuf != NULL) { FREE_DH(recvBuf); CHECK_V_ERROR; }
  if (bdryNodes != NULL) { FREE_DH(bdryNodes); CHECK_V_ERROR; }
  if (marker != NULL) { FREE_DH(marker); CHECK_V_ERROR; }
  if (sendReq!= NULL) { FREE_DH(sendReq); CHECK_V_ERROR; }
  if (recvReq!= NULL) { FREE_DH(recvReq); CHECK_V_ERROR; }
  if (status!= NULL) { FREE_DH(status); CHECK_V_ERROR; }
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "find_ordered_neighbors_private"
void find_ordered_neighbors_private(SubdomainGraph_dh s)
{
  START_FUNC_DH
  HYPRE_Int *loNabors = s->loNabors;
  HYPRE_Int *hiNabors = s->hiNabors;
  HYPRE_Int *allNabors = s->allNabors, allCount = s->allCount;
  HYPRE_Int loCt = 0, hiCt = 0;
  HYPRE_Int *o2n = s->o2n_sub;
  HYPRE_Int i, myNewId = o2n[myid_dh];

  for (i=0; i<allCount; ++i) {
    HYPRE_Int nabor = allNabors[i];
    if (o2n[nabor] < myNewId) {
      loNabors[loCt++] = nabor;
    } else {
      hiNabors[hiCt++] = nabor;
    }
  }

  s->loCount = loCt;
  s->hiCount = hiCt;
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "color_subdomain_graph_private"
void color_subdomain_graph_private(SubdomainGraph_dh s)
{
  START_FUNC_DH
  HYPRE_Int i, n = np_dh;
  HYPRE_Int *rp = s->ptrs, *cval = s->adj;
  HYPRE_Int j, *marker, thisNodesColor, *colorCounter;
  HYPRE_Int *o2n = s->o2n_sub;
  HYPRE_Int *color = s->colorVec;

  if (np_dh == 1) n = s->blocks;

  marker = (HYPRE_Int*)MALLOC_DH((n+1)*sizeof(HYPRE_Int));
  colorCounter = (HYPRE_Int*)MALLOC_DH((n+1)*sizeof(HYPRE_Int));
  for (i=0; i<=n; ++i) {
    marker[i] = -1;
    colorCounter[i] = 0;
  }

  /*------------------------------------------------------------------
   * color the nodes
   *------------------------------------------------------------------*/
  for (i=0; i<n; ++i) {  /* color node "i" */
    /* mark colors of "i"s nabors as unavailable;
       only need to mark nabors that are (per the input ordering)
       numbered less than "i."
     */
    for (j=rp[i]; j<rp[i+1]; ++j) {
      HYPRE_Int nabor = cval[j];
      if (nabor < i) {
        HYPRE_Int naborsColor = color[nabor];
        marker[naborsColor] = i;
      }
    }

    /* assign vertex i the "smallest" possible color */
    thisNodesColor = -1;
    for (j=0; j<n; ++j) {
      if (marker[j] != i) {
        thisNodesColor = j;
        break;
      }
    }
    color[i] = thisNodesColor;
    colorCounter[1+thisNodesColor] += 1;
  }

  /*------------------------------------------------------------------
   * build ordering vector; if two nodes are similarly colored,
   * they will have the same relative ordering as before.
   *------------------------------------------------------------------*/
  /* prefix-sum to find lowest-numbered node for each color */
  for (i=1; i<n; ++i) {
    if (colorCounter[i] == 0) break;
    colorCounter[i] += colorCounter[i-1];
  }

  for (i=0; i<n; ++i) {
    o2n[i] = colorCounter[color[i]];
    colorCounter[color[i]] += 1;
  }

  /* invert permutation */
  invert_perm(n, s->o2n_sub, s->n2o_sub); CHECK_V_ERROR;


  /*------------------------------------------------------------------
   * count the number of colors used
   *------------------------------------------------------------------*/
  { HYPRE_Int ct = 0;
    for (j=0; j<n; ++j) {
      if (marker[j] == -1) break;
      ++ct;
    }
    s->colors = ct;
  }


  /*------------------------------------------------------------------
   * (re)build the beg_rowP array
   *------------------------------------------------------------------*/
  { HYPRE_Int sum = 0;
    for (i=0; i<n; ++i) {
      HYPRE_Int old = s->n2o_sub[i];
      s->beg_rowP[old] = sum;
      sum += s->row_count[old];
    }
  }

  FREE_DH(marker); CHECK_V_ERROR;
  FREE_DH(colorCounter); CHECK_V_ERROR;
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhDump"
void SubdomainGraph_dhDump(SubdomainGraph_dh s, char *filename)
{
  START_FUNC_DH
  HYPRE_Int i;
  HYPRE_Int sCt = np_dh;
  FILE *fp;

  if (np_dh == 1) sCt = s->blocks;


  /* ---------------------------------------------------------
   *  for seq and par runs, 1st processor prints information
   *  that is common to all processors
   * ---------------------------------------------------------*/
  fp=openFile_dh(filename, "w"); CHECK_V_ERROR;

  /* write subdomain ordering permutations */
  hypre_fprintf(fp, "----- colors used\n");
  hypre_fprintf(fp, "%i\n", s->colors);
  if (s->colorVec == NULL) {
    hypre_fprintf(fp, "s->colorVec == NULL\n");
  } else {
    hypre_fprintf(fp, "----- colorVec\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", s->colorVec[i]);
    }
    hypre_fprintf(fp, "\n");
  }

  if (s->o2n_sub == NULL || s->o2n_sub == NULL) {
    hypre_fprintf(fp, "s->o2n_sub == NULL || s->o2n_sub == NULL\n");
  } else {
    hypre_fprintf(fp, "----- o2n_sub\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", s->o2n_sub[i]);
    }
    hypre_fprintf(fp, "\n");
    hypre_fprintf(fp, "----- n2o_sub\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", s->n2o_sub[i]);
    }
    hypre_fprintf(fp, "\n");
  }

  /* write begin row arrays */
  if (s->beg_row == NULL || s->beg_rowP == NULL) {
    hypre_fprintf(fp, "s->beg_row == NULL || s->beg_rowP == NULL\n");
  } else {
    hypre_fprintf(fp, "----- beg_row\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", 1+s->beg_row[i]);
    }
    hypre_fprintf(fp, "\n");
    hypre_fprintf(fp, "----- beg_rowP\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", 1+s->beg_rowP[i]);
    }
    hypre_fprintf(fp, "\n");
  }

  /* write row count  and bdry count arrays */
  if (s->row_count == NULL || s->bdry_count == NULL) {
    hypre_fprintf(fp, "s->row_count == NULL || s->bdry_count == NULL\n");
  } else {
    hypre_fprintf(fp, "----- row_count\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", s->row_count[i]);
    }
    hypre_fprintf(fp, "\n");
    hypre_fprintf(fp, "----- bdry_count\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i ", s->bdry_count[i]);
    }
    hypre_fprintf(fp, "\n");

  }

  /* write subdomain graph */
  if (s->ptrs == NULL || s->adj == NULL) {
    hypre_fprintf(fp, "s->ptrs == NULL || s->adj == NULL\n");
  } else {
    HYPRE_Int j;
    HYPRE_Int ct;
    hypre_fprintf(fp, "----- subdomain graph\n");
    for (i=0; i<sCt; ++i) {
      hypre_fprintf(fp, "%i :: ", i);
      ct = s->ptrs[i+1] - s->ptrs[i];
      if (ct) {
        shellSort_int(ct, s->adj+s->ptrs[i]); CHECK_V_ERROR;
      }
      for (j=s->ptrs[i]; j<s->ptrs[i+1]; ++j) {
        hypre_fprintf(fp, "%i ", s->adj[j]);
      }
      hypre_fprintf(fp, "\n");
    }
  }
  closeFile_dh(fp); CHECK_V_ERROR;

  /* ---------------------------------------------------------
   *  next print info that differs across processors for par
   *  trials.  deal with this as two cases: seq and par
   * ---------------------------------------------------------*/
  if (s->beg_rowP == NULL) {
    SET_V_ERROR("s->beg_rowP == NULL; can't continue");
  }
  if (s->row_count == NULL) {
    SET_V_ERROR("s->row_count == NULL; can't continue");
  }
  if (s->o2n_sub == NULL) {
    SET_V_ERROR("s->o2n_sub == NULL; can't continue");
  }


  if (np_dh == 1) {
    fp=openFile_dh(filename, "a"); CHECK_V_ERROR;

    /* write n2o_row  and o2n_col */
    if (s->n2o_row == NULL|| s->o2n_col == NULL) {
      hypre_fprintf(fp, "s->n2o_row == NULL|| s->o2n_col == NULL\n");
    } else {
      hypre_fprintf(fp, "----- n2o_row\n");
      for (i=0; i<s->m; ++i) {
        hypre_fprintf(fp, "%i ", 1+s->n2o_row[i]);
      }
      hypre_fprintf(fp, "\n");

#if 0
/*
note: this won't match the parallel case, since
      parallel permutation vecs are zero-based and purely
      local
*/

      hypre_fprintf(fp, "----- o2n_col\n");
      for (i=0; i<sCt; ++i) {
        HYPRE_Int br = s->beg_row[i];
        HYPRE_Int er = br + s->row_count[i];

        for (j=br; j<er; ++j) {
          hypre_fprintf(fp, "%i ", 1+s->o2n_col[j]);
        }
        hypre_fprintf(fp, "\n");
      }
      hypre_fprintf(fp, "\n");

#endif

    }
    closeFile_dh(fp); CHECK_V_ERROR;
   }

  /* parallel case */
  else {
    HYPRE_Int id = s->n2o_sub[myid_dh];
    HYPRE_Int m = s->m;
    HYPRE_Int pe;
    HYPRE_Int beg_row = 0;
    if (s->beg_row != 0) beg_row = s->beg_row[myid_dh];

    /* write n2o_row */
    for (pe=0; pe<np_dh; ++pe) {
      hypre_MPI_Barrier(comm_dh);
      if (id == pe) {
        fp=openFile_dh(filename, "a"); CHECK_V_ERROR;
        if (id == 0) hypre_fprintf(fp, "----- n2o_row\n");

        for (i=0; i<m; ++i) {
          hypre_fprintf(fp, "%i ", 1+s->n2o_row[i]+beg_row);
        }
        if (id == np_dh - 1) hypre_fprintf(fp, "\n");
        closeFile_dh(fp); CHECK_V_ERROR;
      }
    }

#if 0

    /* write o2n_col */
    for (pe=0; pe<np_dh; ++pe) {
      hypre_MPI_Barrier(comm_dh);
      if (myid_dh == pe) {
        fp=openFile_dh(filename, "a"); CHECK_V_ERROR;
        if (myid_dh == 0) hypre_fprintf(fp, "----- o2n_col\n");

        for (i=0; i<m; ++i) {
          hypre_fprintf(fp, "%i ", 1+s->o2n_col[i]+beg_row);
        }
        hypre_fprintf(fp, "\n");

        if (myid_dh == np_dh - 1) hypre_fprintf(fp, "\n");

        closeFile_dh(fp); CHECK_V_ERROR;
      }
    }

#endif

  }

  END_FUNC_DH
}



#undef __FUNC__
#define __FUNC__ "find_bdry_nodes_seq_private"
void find_bdry_nodes_seq_private(SubdomainGraph_dh s, HYPRE_Int m, void* A)
{
  START_FUNC_DH
  HYPRE_Int i, j, row, blocks = s->blocks;
  HYPRE_Int *cval, *tmp;

    tmp = (HYPRE_Int*)MALLOC_DH(m*sizeof(HYPRE_Int)); CHECK_V_ERROR;
    for (i=0; i<m; ++i) tmp[i] = 0;

    /*------------------------------------------
     * mark all boundary nodes
     *------------------------------------------ */
    for (i=0; i<blocks; ++i) {
      HYPRE_Int beg_row = s->beg_row[i];
      HYPRE_Int end_row = beg_row + s->row_count[i];

      for (row=beg_row; row<end_row; ++row) {
        bool isBdry = false;
        HYPRE_Int len;
        EuclidGetRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;

        for (j=0; j<len; ++j) { /* for each column in the row */
          HYPRE_Int col = cval[j];

          if (col < beg_row  ||  col >= end_row) {
            tmp[col] = 1;
            isBdry = true;
          }
        }
        if (isBdry) tmp[row] = 1;
        EuclidRestoreRow(A, row, &len, &cval, NULL); CHECK_V_ERROR;
      }
    }

    /*------------------------------------------
     * fill in the bdry_count[] array
     *------------------------------------------ */
    for (i=0; i<blocks; ++i) {
      HYPRE_Int beg_row = s->beg_row[i];
      HYPRE_Int end_row = beg_row + s->row_count[i];
      HYPRE_Int ct = 0;
      for (row=beg_row; row<end_row; ++row) {
        if (tmp[row]) ++ct;
      }
      s->bdry_count[i] = ct;
    }

    /*------------------------------------------
     * form the o2n_col[] permutation
     *------------------------------------------ */
    for (i=0; i<blocks; ++i) {
      HYPRE_Int beg_row = s->beg_row[i];
      HYPRE_Int end_row = beg_row + s->row_count[i];
      HYPRE_Int interiorIDX = beg_row;
      HYPRE_Int bdryIDX = end_row - s->bdry_count[i];

      for (row=beg_row; row<end_row; ++row) {
        if (tmp[row]) {
          s->o2n_col[row] = bdryIDX++;
        } else {
          s->o2n_col[row] = interiorIDX++;
        }
      }
    }

    /* invert permutation */
    invert_perm(m, s->o2n_col, s->n2o_row); CHECK_V_ERROR;
    FREE_DH(tmp); CHECK_V_ERROR;

  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhPrintSubdomainGraph"
void SubdomainGraph_dhPrintSubdomainGraph(SubdomainGraph_dh s, FILE *fp)
{
  START_FUNC_DH
  if (myid_dh == 0) {
    HYPRE_Int i, j;

    hypre_fprintf(fp, "\n-----------------------------------------------------\n");
    hypre_fprintf(fp, "SubdomainGraph, and coloring and ordering information\n");
    hypre_fprintf(fp, "-----------------------------------------------------\n");
    hypre_fprintf(fp, "colors used: %i\n", s->colors);

    hypre_fprintf(fp, "o2n ordering vector: ");
    for (i=0; i<s->blocks; ++i) hypre_fprintf(fp, "%i ", s->o2n_sub[i]);

    hypre_fprintf(fp, "\ncoloring vector (node, color): \n");
    for (i=0; i<s->blocks; ++i) hypre_fprintf(fp, "  %i, %i\n", i, s->colorVec[i]);

    hypre_fprintf(fp, "\n");
    hypre_fprintf(fp, "Adjacency lists:\n");

    for (i=0; i<s->blocks; ++i) {
      hypre_fprintf(fp, "   P_%i :: ", i);
      for (j=s->ptrs[i]; j<s->ptrs[i+1]; ++j) {
        hypre_fprintf(fp, "%i ", s->adj[j]);
      }
      hypre_fprintf(fp, "\n");
    }
    hypre_fprintf(fp, "-----------------------------------------------------\n");
  }
  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "adjust_matrix_perms_private"
void adjust_matrix_perms_private(SubdomainGraph_dh s, HYPRE_Int m)
{
  START_FUNC_DH
  HYPRE_Int i, j, blocks = s->blocks;
  HYPRE_Int *o2n = s->o2n_col;

  for (i=0; i<blocks; ++i) {
    HYPRE_Int beg_row = s->beg_row[i];
    HYPRE_Int end_row = beg_row + s->row_count[i];
    HYPRE_Int adjust = s->beg_rowP[i] - s->beg_row[i];
    for (j=beg_row; j<end_row; ++j) o2n[j] += adjust;
  }

  invert_perm(m, s->o2n_col, s->n2o_row); CHECK_V_ERROR;
  END_FUNC_DH
}

#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhPrintRatios"
void SubdomainGraph_dhPrintRatios(SubdomainGraph_dh s, FILE *fp)
{
  START_FUNC_DH
  HYPRE_Int i;
  HYPRE_Int blocks = np_dh;
  HYPRE_Real ratio[25];

  if (myid_dh == 0) {
    if (np_dh == 1) blocks = s->blocks;
    if (blocks > 25) blocks = 25;

    hypre_fprintf(fp, "\n");
    hypre_fprintf(fp, "Subdomain interior/boundary node ratios\n");
    hypre_fprintf(fp, "---------------------------------------\n");

    /* compute ratios */
    for (i=0; i<blocks; ++i) {
      if (s->bdry_count[i] == 0) {
        ratio[i] = -1;
      } else {
        ratio[i] = (HYPRE_Real)(s->row_count[i] - s->bdry_count[i])/(HYPRE_Real)s->bdry_count[i];
      }
    }

    /* sort ratios */
    shellSort_float(blocks, ratio);

    /* print ratios */
    if (blocks <= 20) {  /* print all ratios */
      HYPRE_Int j = 0;
      for (i=0; i<blocks; ++i) {
        hypre_fprintf(fp, "%0.2g  ", ratio[i]);
        ++j;
        if (j == 10) { hypre_fprintf(fp, "\n"); }
      }
      hypre_fprintf(fp, "\n");
    }
    else {  /* print 10 largest and 10 smallest ratios */
      hypre_fprintf(fp, "10 smallest ratios: ");
      for (i=0; i<10; ++i) {
        hypre_fprintf(fp, "%0.2g  ", ratio[i]);
      }
      hypre_fprintf(fp, "\n");
      hypre_fprintf(fp, "10 largest ratios:  ");
      { HYPRE_Int start = blocks-6, stop = blocks-1;
      for (i=start; i < stop; ++i) {
        hypre_fprintf(fp, "%0.2g  ", ratio[i]);
      }
      hypre_fprintf(fp, "\n");
    }
  }
 }

  END_FUNC_DH
}


#undef __FUNC__
#define __FUNC__ "SubdomainGraph_dhPrintStats"
void SubdomainGraph_dhPrintStats(SubdomainGraph_dh sg, FILE *fp)
{
  START_FUNC_DH
  HYPRE_Real *timing = sg->timing;

  fprintf_dh(fp, "\nSubdomainGraph timing report\n");
  fprintf_dh(fp, "-----------------------------\n");
  fprintf_dh(fp, "total setup time: %0.2f\n", timing[TOTAL_SGT]);
  fprintf_dh(fp, "  find neighbors in subdomain graph: %0.2f\n", timing[FIND_NABORS_SGT]);
  fprintf_dh(fp, "  locally order interiors and bdry:  %0.2f\n", timing[ORDER_BDRY_SGT]);
  fprintf_dh(fp, "  form and color subdomain graph:    %0.2f\n", timing[FORM_GRAPH_SGT]);
  fprintf_dh(fp, "  exchange bdry permutations:        %0.2f\n", timing[EXCHANGE_PERMS_SGT]);
  fprintf_dh(fp, "  everything else (should be small): %0.2f\n",
                  timing[TOTAL_SGT] - (timing[FIND_NABORS_SGT]+
                  timing[ORDER_BDRY_SGT]+timing[FORM_GRAPH_SGT]+
                  timing[EXCHANGE_PERMS_SGT]));
  END_FUNC_DH
}
