/*
 * GraphBLAS Template Library, Version 2.0
 *
 * Copyright 2018 Carnegie Mellon University, Battelle Memorial Institute, and
 * Authors. All Rights Reserved.
 *
 * THIS MATERIAL WAS PREPARED AS AN ACCOUNT OF WORK SPONSORED BY AN AGENCY OF
 * THE UNITED STATES GOVERNMENT.  NEITHER THE UNITED STATES GOVERNMENT NOR THE
 * UNITED STATES DEPARTMENT OF ENERGY, NOR THE UNITED STATES DEPARTMENT OF
 * DEFENSE, NOR CARNEGIE MELLON UNIVERSITY, NOR BATTELLE, NOR ANY OF THEIR
 * EMPLOYEES, NOR ANY JURISDICTION OR ORGANIZATION THAT HAS COOPERATED IN THE
 * DEVELOPMENT OF THESE MATERIALS, MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
 * ASSUMES ANY LEGAL LIABILITY OR RESPONSIBILITY FOR THE ACCURACY, COMPLETENESS,
 * OR USEFULNESS OR ANY INFORMATION, APPARATUS, PRODUCT, SOFTWARE, OR PROCESS
 * DISCLOSED, OR REPRESENTS THAT ITS USE WOULD NOT INFRINGE PRIVATELY OWNED
 * RIGHTS..
 *
 * Released under a BSD (SEI)-style license, please see license.txt or contact
 * permission@sei.cmu.edu for full terms.
 *
 * This release is an update of:
 *
 * 1. GraphBLAS Template Library (GBTL)
 * (https://github.com/cmu-sei/gbtl/blob/1.0.0/LICENSE) Copyright 2015 Carnegie
 * Mellon University and The Trustees of Indiana. DM17-0037, DM-0002659
 *
 * DM18-0559
 */

/**
 * Implementation of sparse mxm for the sequential (CPU) backend.
 */

#ifndef GB_SEQUENTIAL_SPARSE_MXM_ABT_HPP
#define GB_SEQUENTIAL_SPARSE_MXM_ABT_HPP

#pragma once

#include <functional>
#include <utility>
#include <vector>
#include <iterator>
#include <iostream>
#include <chrono>

#include <graphblas/detail/logging.h>
#include <graphblas/types.hpp>
#include <graphblas/algebra.hpp>

#include "sparse_helpers.hpp"
#include "LilSparseMatrix.hpp"


//****************************************************************************

namespace GraphBLAS
{
    namespace backend
    {

        //**********************************************************************
        //**********************************************************************

        //**********************************************************************
        // Perform C = A +.* B' where C, A, and B are all unique
        template<typename CScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_NoMask_NoAccum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B)
        {
            typedef typename SemiringT::result_type D3ScalarType;
            typename LilSparseMatrix<CScalarT>::RowType C_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                C_row.clear();
                if (A[i].empty()) continue;

                // fill row i of T
                for (IndexType j = 0; j < B.nrows(); ++j)
                {
                    if (B[j].empty()) continue;

                    D3ScalarType t_ij;

                    // Perform the dot product
                    // C[i][j] = T_ij = (CScalarT) (A[i] . B[j])
                    if (dot(t_ij, A[i], B[j], semiring))
                    {
                        C_row.push_back(
                            std::make_tuple(j, static_cast<CScalarT>(t_ij)));
                    }
                }

                C.setRow(i, C_row);  // set even if it is empty.
            }
        }

        //**********************************************************************
        // Perform C = C + AB', where C, A, and B must all be unique
        template<typename CScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_NoMask_Accum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B)
        {

            typedef typename SemiringT::result_type D3ScalarType;
            typedef std::vector<std::tuple<IndexType,D3ScalarType> > TRowType;

            TRowType T_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                if (A[i].empty()) continue;

                T_row.clear();

                // Compute row i of T
                // T[i] = (CScalarT) (A[i] *.+ B')
                for (IndexType j = 0; j < B.nrows(); ++j)
                {
                    if (B[j].empty()) continue;

                    D3ScalarType t_ij;

                    // Perform the dot product
                    // T[i][j] = (CScalarT) (A[i] . B[j])
                    if (dot(t_ij, A[i], B[j], semiring))
                    {
                        T_row.push_back(std::make_tuple(j, t_ij));
                    }
                }

                if (!T_row.empty())
                {
                    // C[i] = C[i] + T[i]
                    C.mergeRow(i, T_row, accum);
                }
            }
        }

        //**********************************************************************
        // Perform C<M,z> = A +.* B' where A, B, M, and C are all unique
        template<typename CScalarT,
                 typename MScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_Mask_NoAccum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            typedef typename SemiringT::result_type D3ScalarType;
            typename LilSparseMatrix<D3ScalarType>::RowType T_row;
            typename LilSparseMatrix<CScalarT>::RowType C_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                T_row.clear();

                // T[i] = M[i] .* (A[i] dot B[j])
                if (!A[i].empty() && !M[i].empty())
                {
                    auto M_iter(M[i].begin());
                    for (IndexType j = 0; j < B.nrows(); ++j)
                    {
                        if (B[j].empty() ||
                            !advance_and_check_mask_iterator(M_iter, M[i].end(), j))
                            continue;

                        // Perform the dot product
                        D3ScalarType t_ij;
                        if (dot(t_ij, A[i], B[j], semiring))
                        {
                            T_row.push_back(std::make_tuple(j, t_ij));
                        }
                    }
                }

                if (replace_flag)
                {
                    // C[i] = T[i], z = "replace"
                    C.setRow(i, T_row);
                }
                else /* merge */
                {
                    // C[i] = [!M .* C]  U  T[i], z = "merge"
                    C_row.clear();
                    masked_merge(C_row, M[i], false, C[i], T_row);
                    C.setRow(i, C_row);
                }
            }
        }

        //**********************************************************************
        // Perform C<M,z> = C + (A +.* B') where A, B, M, and C are all unique
        template<typename CScalarT,
                 typename MScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_Mask_Accum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {

            typedef typename SemiringT::result_type D3ScalarType;
            typedef typename AccumT::result_type ZScalarType;
            typename LilSparseMatrix<D3ScalarType>::RowType T_row;
            typename LilSparseMatrix<ZScalarType>::RowType  Z_row;
            typename LilSparseMatrix<CScalarT>::RowType     C_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                T_row.clear();

                if (!A[i].empty() && !M[i].empty())
                {
                    auto m_it(M[i].begin());

                    // Compute: T[i] = M[i] .* {C[i] + (A +.* B')[i]}
                    for (IndexType j = 0; j < B.nrows(); ++j)
                    {
                        // See if B[j] has data and M[i] allows write.
                        if (B[j].empty() ||
                            !advance_and_check_mask_iterator(m_it, M[i].end(), j))
                        {
                            continue;
                        }

                        // Perform the dot product and accum if necessary
                        D3ScalarType t_ij;
                        if (dot(t_ij, A[i], B[j], semiring))
                        {
                            T_row.push_back(std::make_tuple(j, t_ij));
                        }
                    }
                }

                // Z[i] = (M .* C) + T[i]
                Z_row.clear();
                masked_accum(Z_row, M[i], false, accum, C[i], T_row);

                if (replace_flag)
                {
                    C.setRow(i, Z_row);
                }
                else /* merge */
                {
                    // C[i] := (!M[i] .* C[i])  U  Z[i]
                    C_row.clear();
                    masked_merge(C_row, M[i], false, C[i], Z_row);
                    C.setRow(i, C_row);  // set even if it is empty.
                }
            }
        }

        //**********************************************************************
        // Perform C<!M,z> = (A +.* B') where A, B, M, and C are all unique
        template<typename CScalarT,
                 typename MScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_CompMask_NoAccum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            typedef typename SemiringT::result_type D3ScalarType;
            typename LilSparseMatrix<D3ScalarType>::RowType T_row;
            typename LilSparseMatrix<CScalarT>::RowType C_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                T_row.clear();

                // T[i] = !M[i] .* (A[i] dot B[j])
                if (!A[i].empty()) // && !M[i].empty()) cannot do mask shortcut
                {
                    auto M_iter(M[i].begin());
                    for (IndexType j = 0; j < B.nrows(); ++j)
                    {
                        if (B[j].empty() ||
                            advance_and_check_mask_iterator(M_iter, M[i].end(), j))
                            continue;

                        // Perform the dot product
                        D3ScalarType t_ij;
                        if (dot(t_ij, A[i], B[j], semiring))
                        {
                            T_row.push_back(std::make_tuple(j, t_ij));
                        }
                    }
                }

                if (replace_flag)
                {
                    // C[i] = T[i], z = "replace"
                    C.setRow(i, T_row);
                }
                else /* merge */
                {
                    // C[i] = [M .* C]  U  T[i], z = "merge"
                    C_row.clear();
                    masked_merge(C_row, M[i], true, C[i], T_row);
                    C.setRow(i, C_row);
                }
            }
        }

        //**********************************************************************
        // Perform C<!M,z> = C + (A +.* B) where A, B, M, and C are all unique
        template<typename CScalarT,
                 typename MScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void ABT_CompMask_Accum_kernel(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {

            typedef typename SemiringT::result_type D3ScalarType;
            typedef typename AccumT::result_type ZScalarType;
            typename LilSparseMatrix<D3ScalarType>::RowType T_row;
            typename LilSparseMatrix<ZScalarType>::RowType  Z_row;
            typename LilSparseMatrix<CScalarT>::RowType     C_row;

            for (IndexType i = 0; i < A.nrows(); ++i)
            {
                T_row.clear();

                if (!A[i].empty()) // && !M[i].empty()) cannot do mask shortcut
                {
                    auto m_it(M[i].begin());

                    // Compute: T[i] = M[i] .* {C[i] + (A +.* B')[i]}
                    for (IndexType j = 0; j < B.nrows(); ++j)
                    {
                        // See if B[j] has data and M[i] allows write.
                        if (B[j].empty() ||
                            advance_and_check_mask_iterator(m_it, M[i].end(), j))
                        {
                            continue;
                        }

                        // Perform the dot product and accum if necessary
                        D3ScalarType t_ij;
                        if (dot(t_ij, A[i], B[j], semiring))
                        {
                            T_row.push_back(std::make_tuple(j, t_ij));
                        }
                    }
                }

                // Z[i] = (M .* C) + T[i]
                Z_row.clear();
                masked_accum(Z_row, M[i], true, accum, C[i], T_row);


                if (replace_flag)
                {
                    C.setRow(i, Z_row);
                }
                else /* merge */
                {
                    // T[i] := (M[i] .* C[i])  U  Z[i]
                    C_row.clear();
                    masked_merge(C_row, M[i], true, C[i], Z_row);
                    C.setRow(i, C_row);  // set even if it is empty.
                }
            }
        }

        //**********************************************************************
        //**********************************************************************
        //**********************************************************************

        //**********************************************************************
        template<typename CScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_NoMask_NoAccum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B)
        {
            //std::cout << "sparse_mxm_NoMask_NoAccum_ABT COMPLETED.\n";

            // C = (A +.* B')
            // short circuit conditions
            if ((A.nvals() == 0) || (B.nvals() == 0))
            {
                C.clear();
                return;
            }

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                //std::cout << "ABT USING CTMP\n";
                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<CScalarT> Ctmp(C.nrows(), C.ncols());
                ABT_NoMask_NoAccum_kernel(Ctmp, semiring, A, B);
                C.swap(Ctmp);
            }
            else
            {
                ABT_NoMask_NoAccum_kernel(C, semiring, A, B);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

        //**********************************************************************
        template<typename CScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_NoMask_Accum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B)
        {
            //std::cout << "sparse_mxm_NoMask_Accum_ABT COMPLETED.\n";

            // C = C + (A +.* B')
            // short circuit conditions?
            if ((A.nvals() == 0) || (B.nvals() == 0))
            {
                return;  // do nothing
            }

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<CScalarT> Ctmp(C.nrows(), C.ncols());
                ABT_NoMask_NoAccum_kernel(Ctmp, semiring, A, B);
                for (IndexType i = 0; i < C.nrows(); ++i)
                {
                    C.mergeRow(i, Ctmp[i], accum);
                }
            }
            else
            {
                ABT_NoMask_Accum_kernel(C, accum, semiring, A, B);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

        //**********************************************************************
        template<typename CScalarT,
                 typename MScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_Mask_NoAccum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            //std::cout << "sparse_mxm_Mask_NoAccum_ABT COMPLETED.\n";

            // C<M,z> = (A +.* B')
            //        =             [M .* (A +.* B')], z = replace
            //        = [!M .* C] U [M .* (A +.* B')], z = merge
            // short circuit conditions
            if (replace_flag &&
                ((A.nvals() == 0) || (B.nvals() == 0) || (M.nvals() == 0)))
            {
                C.clear();
                return;
            }
            else if (!replace_flag && (M.nvals() == 0))
            {
                return; // do nothing
            }

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<CScalarT> Ctmp(C.nrows(), C.ncols());
                ABT_Mask_NoAccum_kernel(Ctmp, M, semiring, A, B, true);

                if (replace_flag)
                {
                    C.swap(Ctmp);
                }
                else
                {
                    typename LilSparseMatrix<CScalarT>::RowType C_row;
                    for (IndexType i = 0; i < C.nrows(); ++i)
                    {
                        // C[i] = [!M .* C]  U  T[i], z = "merge"
                        C_row.clear();
                        masked_merge(C_row, M[i], false, C[i], Ctmp[i]);
                        C.setRow(i, C_row);
                    }
                }
            }
            else
            {
                ABT_Mask_NoAccum_kernel(C, M, semiring, A, B, replace_flag);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

        //**********************************************************************
        template<typename CScalarT,
                 typename MScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_Mask_Accum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            //std::cout << "sparse_mxm_Mask_Accum_ABT COMPLETED.\n";

            // C<M,z> = C + (A +.* B')
            //        =               [M .* [C + (A +.* B')]], z = "replace"
            //        = [!M .* C]  U  [M .* [C + (A +.* B')]], z = "merge"
            // short circuit conditions
            if (replace_flag && (M.nvals() == 0))
            {
                C.clear();
                return;
            }
            else if (!replace_flag && (M.nvals() == 0))
            {
                return; // do nothing
            }

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                typedef typename SemiringT::result_type D3ScalarType;
                typedef typename AccumT::result_type ZScalarType;

                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<D3ScalarType> Ctmp(C.nrows(), C.ncols());
                ABT_Mask_NoAccum_kernel(Ctmp, M, semiring, A, B, true);

                typename LilSparseMatrix<ZScalarType>::RowType  Z_row;

                for (IndexType i = 0; i < C.nrows(); ++i)
                {
                    Z_row.clear();
                    // Z[i] = (M .* C) + Ctmp[i]
                    masked_accum(Z_row, M[i], false, accum, C[i], Ctmp[i]);

                    if (replace_flag)
                    {
                        C.setRow(i, Z_row);
                    }
                    else
                    {
                        typename LilSparseMatrix<CScalarT>::RowType C_row;
                        // C[i] = [!M .* C]  U  Ctmp[i], z = "merge"
                        C_row.clear();
                        masked_merge(C_row, M[i], false, C[i], Z_row);
                        C.setRow(i, C_row);
                    }
                }
            }
            else
            {
                ABT_Mask_Accum_kernel(C, M, accum, semiring, A, B, replace_flag);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

        //**********************************************************************
        template<typename CScalarT,
                 typename MScalarT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_CompMask_NoAccum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            //std::cout << "sparse_mxm_CompMask_NoAccum_ABT COMPLETED.\n";

            // C<M,z> =            [!M .* (A +.* B')], z = replace
            //        = [M .* C] U [!M .* (A +.* B')], z = merge
            // short circuit conditions
            if (replace_flag &&
                ((A.nvals() == 0) || (B.nvals() == 0)))
            {
                C.clear();
                return;
            }
            //else if (!replace_flag && (M.nvals() == 0)) // this is NoMask_NoAccum

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<CScalarT> Ctmp(C.nrows(), C.ncols());
                ABT_CompMask_NoAccum_kernel(Ctmp, M, semiring, A, B, true);

                if (replace_flag)
                {
                    C.swap(Ctmp);
                }
                else
                {
                    typename LilSparseMatrix<CScalarT>::RowType C_row;
                    for (IndexType i = 0; i < C.nrows(); ++i)
                    {
                        // C[i] = [!M .* C]  U  T[i], z = "merge"
                        C_row.clear();
                        masked_merge(C_row, M[i], true, C[i], Ctmp[i]);
                        C.setRow(i, C_row);
                    }
                }
            }
            else
            {
                ABT_CompMask_NoAccum_kernel(C, M, semiring, A, B, replace_flag);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

        //**********************************************************************
        template<typename CScalarT,
                 typename MScalarT,
                 typename AccumT,
                 typename SemiringT,
                 typename AScalarT,
                 typename BScalarT>
        inline void sparse_mxm_CompMask_Accum_ABT(
            LilSparseMatrix<CScalarT>       &C,
            LilSparseMatrix<MScalarT> const &M,
            AccumT                    const &accum,
            SemiringT                        semiring,
            LilSparseMatrix<AScalarT> const &A,
            LilSparseMatrix<BScalarT> const &B,
            bool                             replace_flag)
        {
            //std::cout << "sparse_mxm_CompMask_Accum_ABT COMPLETED.\n";

            // C<M,z> = C + (A +.* B')
            //        =              [!M .* [C + (A +.* B')]], z = "replace"
            //        = [M .* C]  U  [!M .* [C + (A +.* B')]], z = "merge"
            // short circuit conditions: NONE?

            // =================================================================

            if ((void*)&C == (void*)&B)
            {
                typedef typename SemiringT::result_type D3ScalarType;
                typedef typename AccumT::result_type ZScalarType;

                // create temporary to prevent overwrite of inputs
                LilSparseMatrix<D3ScalarType> Ctmp(C.nrows(), C.ncols());
                ABT_CompMask_NoAccum_kernel(Ctmp, M, semiring, A, B, true);

                typename LilSparseMatrix<ZScalarType>::RowType  Z_row;

                for (IndexType i = 0; i < C.nrows(); ++i)
                {
                    Z_row.clear();
                    // Z[i] = (M .* C) + Ctmp[i]
                    masked_accum(Z_row, M[i], true, accum, C[i], Ctmp[i]);

                    if (replace_flag)
                    {
                        C.setRow(i, Z_row);
                    }
                    else
                    {
                        typename LilSparseMatrix<CScalarT>::RowType C_row;
                        // C[i] = [!M .* C]  U  Ctmp[i], z = "merge"
                        C_row.clear();
                        masked_merge(C_row, M[i], true, C[i], Z_row);
                        C.setRow(i, C_row);
                    }
                }
            }
            else
            {
                ABT_CompMask_Accum_kernel(C, M, accum, semiring, A, B, replace_flag);
            }

            GRB_LOG_VERBOSE("C: " << C);
        }

    } // backend
} // GraphBLAS

#endif