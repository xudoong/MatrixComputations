#include <stdio.h>
#include "local_domain.h"

static void print_ldomain(const local_domain_t *ldomain)
{
    int rank = 0;
    int B = ldomain->B;
    while (rank < ldomain->grid->P * ldomain->grid->Q) 
    {
        if (ldomain->grid->lrank == rank) {
            printf("----------------------\n");
            printf("Rank %d local domain:\n", ldomain->grid->lrank);
            for (int bi = 0; bi < ldomain->NUM_BLOCKS; bi++)
            {
                printf("\tBlock %d: ", bi);
                for (int i = 0; i < B * B; i++)
                    printf("%3.0f ", ldomain->data[bi * B * B + i]);
                printf("\n");
            }
            fflush(stdout);
        }
        rank++;
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

void init_local_domain(const double *globalA, int N, int B, const grid_t *grid, local_domain_t *ldomain)
{
    #ifdef DEBUG
    printf("Rank %d: initialing ldomain.\n", grid->lrank);
    #endif
    ldomain->grid = grid;
    ldomain->N = N;
    ldomain->B = B;
    ldomain->NBP = (int)(N / B / grid->P) + (grid->myrow < (int)(N / B) % grid->P);
    ldomain->NBQ = (int)(N / B / grid->Q) + (grid->mycol < (int)(N / B) % grid->Q);
    ldomain->NUM_BLOCKS = ldomain->NBP * ldomain->NBQ;

    ldomain->data = (double *)malloc(ldomain->NUM_BLOCKS * B * B * sizeof(double));

    MPI_Datatype block_type;
    MPI_Type_vector(B, B, N, MPI_DOUBLE, &block_type);
    MPI_Type_commit(&block_type);

    // send global matrix A from rank 0 to other ranks
    if (grid->lrank == 0) // rank 0: send to other ranks
    {
        for (int p = 0; p < grid->P; p++)
        {
            for (int q = 0; q < grid->Q; q++)
            {
                int t_rank = RANK_FROM_PQ(grid, p, q);
                if (t_rank == 0)
                {
                    // target rank 0: copy instead of send
                    for (int lbi = 0; lbi < ldomain->NBP; lbi++)
                    {
                        for (int lbj = 0; lbj < ldomain->NBQ; lbj++)
                        {
                            double *blk = BLK(ldomain, lbi, lbj);
                            for (int ii = 0; ii < B; ii++)
                                for (int jj = 0; jj < B; jj++)
                                {
                                    int gi = G_ROW(ldomain, lbi, ii);
                                    int gj = G_COL(ldomain, lbj, jj);
                                    BLK_ELE(blk, B, ii, jj) = globalA[gi * N + gj];                        
                                }
                        }
                    }
                }
                else
                {
                    for (int lbi = 0; lbi * grid->P + p < (int)(N / B); lbi++)
                    {
                        for (int lbj = 0; lbj * grid->Q + q < (int)(N / B); lbj++)
                        {
                            int gi = G_ROW_P(ldomain, lbi, p, 0);
                            int gj = G_COL_Q(ldomain, lbj, q, 0);
                            #ifdef DEBUG
                            printf("Rank 0: sending to rank %d, block(%d, %d), gij=(%d, %d), first value: %3.0f\n", t_rank, lbi, lbj, gi, gj, globalA[gi * N + gj]);

                            fflush(stdout);
                            #endif
                            MPI_Send(globalA + gi * N + gj, 1, block_type, t_rank, 0, MPI_COMM_WORLD);
                        }
                    }
                }
            }
        }
    }
    else // rank > 0: recv from rank 0
    {
        for (int lbi = 0; lbi < ldomain->NBP; lbi++)
            for (int lbj = 0; lbj < ldomain->NBQ; lbj++)
            {
                MPI_Recv(BLK(ldomain, lbi, lbj), B * B, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                #ifdef DEBUG
                printf("Rank %d: finish recv block(%d, %d) from rank 0.\n", grid->lrank, lbi, lbj);
                fflush(stdout);
                #endif
            }
    }
    

    #ifdef DEBUG
    printf("Rank %d: Done recv initial matrix.\n", grid->lrank);
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);
    if (grid->lrank == 0)
    {
        printf("Global Matrix:\n");
        for (int i = 0; i < N; i++)
        {
            for (int j = 0; j < N; j++)
                printf("%3.0f ", globalA[i * N + j]);
            printf("\n");
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    print_ldomain(ldomain);
    #endif
}