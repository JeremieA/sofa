/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 4      *
*                (c) 2006-2009 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
// Author: Hadrien Courtecuisse
//
// Copyright: See COPYING file that comes with this distribution

#ifndef SOFA_COMPONENT_LINEARSOLVER_PrecomputedWarpPreconditioner_INL
#define SOFA_COMPONENT_LINEARSOLVER_PrecomputedWarpPreconditioner_INL

#include "PrecomputedWarpPreconditioner.h"
#include <sofa/component/linearsolver/NewMatMatrix.h>
#include <sofa/component/linearsolver/FullMatrix.h>
#include <sofa/component/linearsolver/SparseMatrix.h>
#include <iostream>
#include "sofa/helper/system/thread/CTime.h"
#include <sofa/core/objectmodel/BaseContext.h>
#include <sofa/core/componentmodel/behavior/LinearSolver.h>
#include <math.h>
#include <sofa/helper/system/thread/CTime.h>
#include <sofa/component/forcefield/TetrahedronFEMForceField.h>
#include <sofa/defaulttype/Vec3Types.h>
#include <sofa/component/linearsolver/MatrixLinearSolver.h>
#include <sofa/helper/system/thread/CTime.h>
#include <sofa/component/container/RotationFinder.h>
#include <sofa/core/componentmodel/behavior/LinearSolver.h>


#include <sofa/component/odesolver/EulerImplicitSolver.h>
#include <sofa/component/linearsolver/CGLinearSolver.h>
#include <sofa/component/linearsolver/PCGLinearSolver.h>

#ifdef SOFA_HAVE_CSPARSE
#include <sofa/component/linearsolver/SparseCholeskySolver.h>
#include <sofa/component/linearsolver/SparseLDLSolver.h>
#include <sofa/component/linearsolver/CompressedRowSparseMatrix.h>
#else
#include <sofa/component/linearsolver/CholeskySolver.h>
#endif


//#define DISPLAY_TIME

namespace sofa
{

namespace component
{

namespace linearsolver
{

using namespace sofa::component::odesolver;
using namespace sofa::component::linearsolver;

template<class TDataTypes,class TMatrix,class TVector>
PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::PrecomputedWarpPreconditioner()
    : f_verbose( initData(&f_verbose,false,"verbose","Dump system state at each iteration") )
    , use_file( initData(&use_file,true,"use_file","Dump system matrix in a file") )
    , solverName(initData(&solverName, std::string(""), "solverName", "Name of the solver to use to precompute the first matrix"))
    , init_MaxIter( initData(&init_MaxIter,5000,"init_MaxIter","Max Iter use to precompute the first matrix") )
    , init_Tolerance( initData(&init_Tolerance,1e-20,"init_Tolerance","Tolerance use to precompute the first matrix") )
    , init_Threshold( initData(&init_Threshold,1e-35,"init_Threshold","Threshold use to precompute the first matrix") )
{
    first = true;
    _rotate = false;
    usePrecond = true;
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::setSystemMBKMatrix(double mFact, double bFact, double kFact)
{
    // Update the matrix only the first time
    if (first)
    {
        first = false;
        init_mFact = mFact;
        init_bFact = bFact;
        init_kFact = kFact;
        Inherit::setSystemMBKMatrix(mFact,bFact,kFact);
#ifdef VALIDATE_ALGORITM_PrecomputedWarpPreconditioner
        for (unsigned j=0; j<this->currentGroup->systemMatrix->rowSize(); j++) printf("%f ",this->currentGroup->systemMatrix->element(j,j));
        printf("\n");
#endif
        loadMatrix();
    }

#ifdef VALIDATE_ALGORITM_PrecomputedWarpPreconditioner
    else
    {
        this->currentGroup->systemMatrix = realSystem;
        Inherit::setSystemMBKMatrix(mFact,bFact,kFact);
        this->currentGroup->systemMatrix = invertSystem;
    }
    printf("RealSystem(%d,%d) InvertSystem(%d,%d)\n",realSystem->rowSize(),realSystem->colSize(),invertSystem->rowSize(),invertSystem->colSize());
    for (unsigned j=0; j<12; j++)
    {
        for (unsigned i=0; i<12; i++)
        {
            double mult_ij = 0.0;
            for (unsigned k=0; k<invertSystem->rowSize(); k++)
            {
                mult_ij += realSystem->element(j,k) * invertSystem->element(k,i);
            }
            printf("%f ",mult_ij);
        }
        printf("\n");
    }
#endif

    this->currentGroup->needInvert = usePrecond;
}

//Solve x = R * M^-1 * R^t * b
template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::solve (TMatrix& /*M*/, TVector& z, TVector& r)
{
    if (usePrecond)
    {
        unsigned int k = 0;
        unsigned int l = 0;

        //Solve z = R^t * b
        while (l < this->currentGroup->systemMatrix->colSize())
        {
            z[l+0] = R[k + 0] * r[l + 0] + R[k + 3] * r[l + 1] + R[k + 6] * r[l + 2];
            z[l+1] = R[k + 1] * r[l + 0] + R[k + 4] * r[l + 1] + R[k + 7] * r[l + 2];
            z[l+2] = R[k + 2] * r[l + 0] + R[k + 5] * r[l + 1] + R[k + 8] * r[l + 2];
            l+=3;
            k+=9;
        }

        //Solve tmp = M^-1 * z
        T = *(this->currentGroup->systemMatrix) * z;

        //Solve z = R * tmp
        k = 0; l = 0;
        while (l < this->currentGroup->systemMatrix->colSize())
        {
            z[l+0] = R[k + 0] * T[l + 0] + R[k + 1] * T[l + 1] + R[k + 2] * T[l + 2];
            z[l+1] = R[k + 3] * T[l + 0] + R[k + 4] * T[l + 1] + R[k + 5] * T[l + 2];
            z[l+2] = R[k + 6] * T[l + 0] + R[k + 7] * T[l + 1] + R[k + 8] * T[l + 2];
            l+=3;
            k+=9;
        }

    }
    else z = r;
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::loadMatrix()
{
    unsigned systemSize = this->currentGroup->systemMatrix->rowSize();
    dt = this->getContext()->getDt();

#ifdef VALIDATE_ALGORITM_PrecomputedWarpPreconditioner
    this->realSystem = new TMatrix();
    this->realSystem->resize(systemSize,systemSize);
    this->invertSystem = this->currentGroup->systemMatrix;
    for (unsigned int j=0; j<systemSize; j++)
    {
        for (unsigned i=0; i<systemSize; i++)
        {
            this->realSystem->set(j,i,this->currentGroup->systemMatrix->element(j,i));
        }
    }
#endif

    EulerImplicitSolver* EulerSolver;
    this->getContext()->get(EulerSolver);
    factInt = 1.0; // christian : it is not a compliance... but an admittance that is computed !
    if (EulerSolver) factInt = EulerSolver->getPositionIntegrationFactor(); // here, we compute a compliance

    std::stringstream ss;
    ss << this->getContext()->getName() << "-" << systemSize << "-" << dt << ".comp";
    std::ifstream compFileIn(ss.str().c_str(), std::ifstream::binary);

    if(compFileIn.good() && use_file.getValue())
    {
        FullVector<Real> checkSys;
        checkSys.resize(systemSize);
        for (unsigned int j=0; j<systemSize; j++) checkSys[j] = this->currentGroup->systemMatrix->element(0,j);

        cout << "file open : " << ss.str() << " compliance being loaded" << endl;
        compFileIn.read((char*) (*this->currentGroup->systemMatrix)[0], systemSize * systemSize * sizeof(Real));
        compFileIn.close();
    }
    else
    {
        cout << "Precompute : " << ss.str() << " compliance" << endl;
        if (solverName.getValue().empty()) loadMatrixWithCSparse();
        else loadMatrixWithSolver();

        if (use_file.getValue())
        {
            std::ofstream compFileOut(ss.str().c_str(), std::fstream::out | std::fstream::binary);
            compFileOut.write((char*)(*this->currentGroup->systemMatrix)[0], systemSize * systemSize*sizeof(Real));
            compFileOut.close();
        }
    }

    for (unsigned int j=0; j<systemSize; j++)
    {
        for (unsigned i=0; i<systemSize; i++)
        {
            this->currentGroup->systemMatrix->set(j,i,this->currentGroup->systemMatrix->element(j,i)/factInt);
        }
    }

    R.resize(3*systemSize);
    T.resize(systemSize);
    for(unsigned int k = 0; k < systemSize/3; k++)
    {
        R[k*9] = R[k*9+4] = R[k*9+8] = 1.0f;
        R[k*9+1] = R[k*9+2] = R[k*9+3] = R[k*9+5] = R[k*9+6] = R[k*9+7] = 0.0f;
    }
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector>::loadMatrixWithCSparse()
{
#ifdef SOFA_HAVE_CSPARSE
    cout << "Compute the initial invert matrix with CS_PARSE" << endl;

    CompressedRowSparseMatrix<double> matSolv;
    FullVector<double> r;
    FullVector<double> b;

    unsigned systemSize = this->currentGroup->systemMatrix->colSize();

    matSolv.resize(systemSize,systemSize);
    r.resize(systemSize);
    b.resize(systemSize);
    SparseCholeskySolver<CompressedRowSparseMatrix<double>, FullVector<double> > solver;

    for (unsigned int j=0; j<systemSize; j++)
    {
        for (unsigned int i=0; i<systemSize; i++)
        {
            if (this->currentGroup->systemMatrix->element(j,i)!=0) matSolv.set(j,i,(double)this->currentGroup->systemMatrix->element(j,i));
        }
        b.set(j,0.0);
    }

    std::cout << "Precomputing constraint correction LU decomposition " << std::endl;
    solver.invert(matSolv);

    for (unsigned int j=0; j<systemSize; j++)
    {
        std::cout.precision(2);
        std::cout << "Precomputing constraint correction : " << std::fixed << (float)j/(float)systemSize*100.0f << " %   " << '\xd';
        std::cout.flush();

        if (j>0) b.set(j-1,0.0);
        b.set(j,1.0);

        solver.solve(matSolv,r,b);
        for (unsigned int i=0; i<systemSize; i++)
        {
            this->currentGroup->systemMatrix->set(j,i,r.element(i)*factInt);
        }
    }
    std::cout << "Precomputing constraint correction : " << std::fixed << 100.0f << " %   " << '\xd';
    std::cout.flush();

#else
    std::cout << "WARNING ; you don't have CS_parse solvername will be use" << std::endl;
    loadMatrixWithSolver();
#endif
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::loadMatrixWithSolver()
{
    usePrecond = false;//Don'Use precond during precomputing

    cout << "Compute the initial invert matrix with solver" << endl;

    behavior::MechanicalState<DataTypes>* mstate = dynamic_cast< behavior::MechanicalState<DataTypes>* >(this->getContext()->getMechanicalState());
    if (mstate==NULL)
    {
        serr << "PrecomputedWarpPreconditioner can't find Mstate" << sendl;
        return;
    }
    const VecDeriv& v0 = *mstate->getV();
    unsigned dof_on_node = v0[0].size();
    unsigned nbNodes = v0.size();
    unsigned systemSize = nbNodes*dof_on_node;

    std::stringstream ss;
    //ss << this->getContext()->getName() << "_CPP.comp";
    ss << this->getContext()->getName() << "-" << systemSize << "-" << dt << ".comp";
    std::ifstream compFileIn(ss.str().c_str(), std::ifstream::binary);

    EulerImplicitSolver* EulerSolver;
    this->getContext()->get(EulerSolver);

    // for the initial computation, the gravity has to be put at 0
    const Vec3d gravity = this->getContext()->getGravityInWorld();
    const Vec3d gravity_zero(0.0,0.0,0.0);
    this->getContext()->setGravityInWorld(gravity_zero);

    PCGLinearSolver<GraphScatteredMatrix,GraphScatteredVector>* PCGlinearSolver;
    CGLinearSolver<GraphScatteredMatrix,GraphScatteredVector>* CGlinearSolver;
    core::componentmodel::behavior::LinearSolver* linearSolver;

    if (solverName.getValue().empty())
    {
        this->getContext()->get(CGlinearSolver);
        this->getContext()->get(PCGlinearSolver);
        this->getContext()->get(linearSolver);
    }
    else
    {
        core::objectmodel::BaseObject* ptr = NULL;
        this->getContext()->get(ptr, solverName.getValue());
        PCGlinearSolver = dynamic_cast<PCGLinearSolver<GraphScatteredMatrix,GraphScatteredVector>*>(ptr);
        CGlinearSolver = dynamic_cast<CGLinearSolver<GraphScatteredMatrix,GraphScatteredVector>*>(ptr);
        linearSolver = dynamic_cast<core::componentmodel::behavior::LinearSolver*>(ptr);
    }

    if(EulerSolver && CGlinearSolver)
        sout << "use EulerImplicitSolver &  CGLinearSolver" << sendl;
    else if(EulerSolver && PCGlinearSolver)
        sout << "use EulerImplicitSolver &  PCGLinearSolver" << sendl;
    else if(EulerSolver && linearSolver)
        sout << "use EulerImplicitSolver &  LinearSolver" << sendl;
    else if(EulerSolver)
    {
        sout << "use EulerImplicitSolver" << sendl;
    }
    else
    {
        serr<<"PrecomputedContactCorrection must be associated with EulerImplicitSolver+LinearSolver for the precomputation\nNo Precomputation" << sendl;
        return;
    }
    VecId lhId = core::componentmodel::behavior::BaseMechanicalState::VecId::velocity();
    VecId rhId = core::componentmodel::behavior::BaseMechanicalState::VecId::force();


    mstate->vAvail(lhId);
    mstate->vAlloc(lhId);
    mstate->vAvail(rhId);
    mstate->vAlloc(rhId);
    std::cout << "System: (" << init_mFact << " * M + " << init_bFact << " * B + " << init_kFact << " * K) " << lhId << " = " << rhId << std::endl;
    if (linearSolver)
    {
        std::cout << "System Init Solver: " << linearSolver->getName() << " (" << linearSolver->getClassName() << ")" << std::endl;
        linearSolver->setSystemMBKMatrix(init_mFact, init_bFact, init_kFact);
    }

    VecDeriv& force = *mstate->getVecDeriv(rhId.index);
    force.clear();
    force.resize(nbNodes);

    ///////////////////////// CHANGE THE PARAMETERS OF THE SOLVER /////////////////////////////////
    double buf_tolerance=0, buf_threshold=0;
    int buf_maxIter=0;
    if(CGlinearSolver)
    {
        buf_tolerance = (double) CGlinearSolver->f_tolerance.getValue();
        buf_maxIter   = (int) CGlinearSolver->f_maxIter.getValue();
        buf_threshold = (double) CGlinearSolver->f_smallDenominatorThreshold.getValue();
        CGlinearSolver->f_tolerance.setValue(init_Tolerance.getValue());
        CGlinearSolver->f_maxIter.setValue(init_MaxIter.getValue());
        CGlinearSolver->f_smallDenominatorThreshold.setValue(init_Threshold.getValue());
    }
    else if(PCGlinearSolver)
    {
        buf_tolerance = (double) PCGlinearSolver->f_tolerance.getValue();
        buf_maxIter   = (int) PCGlinearSolver->f_maxIter.getValue();
        buf_threshold = (double) PCGlinearSolver->f_smallDenominatorThreshold.getValue();
        PCGlinearSolver->f_tolerance.setValue(init_Tolerance.getValue());
        PCGlinearSolver->f_maxIter.setValue(init_MaxIter.getValue());
        PCGlinearSolver->f_smallDenominatorThreshold.setValue(init_Threshold.getValue());
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////

    VecDeriv& velocity = *mstate->getVecDeriv(lhId.index);
    VecDeriv velocity0 = velocity;
    VecCoord& pos = *mstate->getX();
    VecCoord pos0 = pos;

    for(unsigned int f = 0 ; f < nbNodes ; f++)
    {
        std::cout.precision(2);
        std::cout << "Precomputing constraint correction : " << std::fixed << (float)f/(float)nbNodes*100.0f << " %   " << '\xd';
        std::cout.flush();
        Deriv unitary_force;

        for (unsigned int i=0; i<dof_on_node; i++)
        {
            unitary_force.clear();
            unitary_force[i]=1.0;
            force[f] = unitary_force;

            velocity.clear();
            velocity.resize(nbNodes);

            if(f*dof_on_node+i <2 )
            {
                EulerSolver->f_verbose.setValue(true);
                EulerSolver->f_printLog.setValue(true);
                serr<<"getF : "<<force<<sendl;
            }

            if (linearSolver)
            {
                linearSolver->setSystemRHVector(rhId);
                linearSolver->setSystemLHVector(lhId);
                linearSolver->solveSystem();
            }

            if (linearSolver && f*dof_on_node+i == 0) linearSolver->freezeSystemMatrix(); // do not recompute the matrix for the rest of the precomputation

            if(f*dof_on_node+i < 2)
            {
                EulerSolver->f_verbose.setValue(false);
                EulerSolver->f_printLog.setValue(false);
                serr<<"getV : "<<velocity<<sendl;
            }
            for (unsigned int v=0; v<nbNodes; v++)
            {
                for (unsigned int j=0; j<dof_on_node; j++)
                {
                    this->currentGroup->systemMatrix->set(v*dof_on_node+j,f*dof_on_node+i,(Real)(velocity[v][j]*factInt));
                }
            }
        }
        unitary_force.clear();
        force[f] = unitary_force;
    }
    std::cout << "Precomputing constraint correction : " << std::fixed << 100.0f << " %   " << '\xd';
    std::cout.flush();

    ///////////////////////////////////////////////////////////////////////////////////////////////

    if (linearSolver) linearSolver->updateSystemMatrix(); // do not recompute the matrix for the rest of the precomputation

    ///////////////////////// RESET PARAMETERS AT THEIR PREVIOUS VALUE /////////////////////////////////
    // gravity is reset at its previous value
    this->getContext()->setGravityInWorld(gravity);

    if(CGlinearSolver)
    {
        CGlinearSolver->f_tolerance.setValue(buf_tolerance);
        CGlinearSolver->f_maxIter.setValue(buf_maxIter);
        CGlinearSolver->f_smallDenominatorThreshold.setValue(buf_threshold);
    }
    else if(PCGlinearSolver)
    {
        PCGlinearSolver->f_tolerance.setValue(buf_tolerance);
        PCGlinearSolver->f_maxIter.setValue(buf_maxIter);
        PCGlinearSolver->f_smallDenominatorThreshold.setValue(buf_threshold);
    }

    //Reset the velocity
    for (unsigned int i=0; i<velocity0.size(); i++) velocity[i]=velocity0[i];
    //Reset the position
    for (unsigned int i=0; i<pos0.size(); i++) pos[i]=pos0[i];

    mstate->vFree(lhId);
    mstate->vFree(rhId);

    usePrecond = true;
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::invert(TMatrix& /*M*/)
{
    _rotate = true;
    this->rotateConstraints();
}

template<class TDataTypes,class TMatrix,class TVector>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::rotateConstraints()
{
    unsigned systemSize3 = this->currentGroup->systemMatrix->colSize()/3;
    if (R.size() != systemSize3*9)
    {
        T.resize(this->currentGroup->systemMatrix->colSize());
        R.resize(systemSize3*9);
    }

    simulation::Node *node = dynamic_cast<simulation::Node *>(this->getContext());
    sofa::component::forcefield::TetrahedronFEMForceField<TDataTypes>* forceField = NULL;
    //sofa::component::container::RotationFinder<TDataTypes>* rotationFinder = NULL;

    if (node != NULL)
    {
        forceField = node->get<component::forcefield::TetrahedronFEMForceField<TDataTypes> > ();
        if (forceField == NULL)
        {
            //rotationFinder = node->get<component::container::RotationFinder<TDataTypes> > ();
            //if (rotationFinder == NULL)
            sout << "No rotation defined : only defined for TetrahedronFEMForceField and RotationFinder!";

        }
    }

    Transformation Rotation;
    if (forceField != NULL)
    {
        for(unsigned int k = 0; k < systemSize3; k++)
        {
            forceField->getRotation(Rotation, k);
            for (int j=0; j<3; j++)
            {
                for (int i=0; i<3; i++)
                {
                    R[k*9+j*3+i] = (Real)Rotation[j][i];
                }
            }
        }
// 	} else if (rotationFinder != NULL) {
// 		const helper::vector<defaulttype::Mat<3,3,Real> > & rotations = rotationFinder->getRotations();
// 		for(unsigned int k = 0; k < systemSize3; k++)	{
// 			Rotation = rotations[k];
// 			for (int j=0;j<3;j++) {
// 				for (int i=0;i<3;i++) {
// 					R[k*9+j*3+i] = (Real)Rotation[j][i];
// 				}
// 			}
// 		}
    }
    else
    {
        serr << "No rotation defined : use Identity !!";
        for(unsigned int k = 0; k < systemSize3; k++)
        {
            R[k*9] = R[k*9+4] = R[k*9+8] = 1.0f;
            R[k*9+1] = R[k*9+2] = R[k*9+3] = R[k*9+5] = R[k*9+6] = R[k*9+7] = 0.0f;
        }
    }


}

template<class TDataTypes,class TMatrix,class TVector>
bool PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector >::addJMInvJt(defaulttype::BaseMatrix* result, defaulttype::BaseMatrix* J, double fact)
{
    if (! _rotate) this->rotateConstraints();  //already rotate with Preconditionner
    _rotate = false;

    if (J->colSize() == 0) return true;

    if (SparseMatrix<double>* j = dynamic_cast<SparseMatrix<double>*>(J))
    {
        ComputeResult(result, *j, (float) fact);
    }
    else if (SparseMatrix<float>* j = dynamic_cast<SparseMatrix<float>*>(J))
    {
        ComputeResult(result, *j, (float) fact);
    } return false;

    return true;
}

template<class TDataTypes,class TMatrix,class TVector> template<class JMatrix>
void PrecomputedWarpPreconditioner<TDataTypes,TMatrix,TVector>::ComputeResult(defaulttype::BaseMatrix * result,JMatrix& J, float fact)
{
    internalData.JR.clear();
    internalData.JR.resize(J.rowSize(),J.colSize());

    //compute JR = J * R
    unsigned nl = 0;
    for (typename JMatrix::LineConstIterator jit1 = J.begin(); jit1 != J.end(); jit1++)
    {
        int l = jit1->first;
        for (typename JMatrix::LElementConstIterator i1 = jit1->second.begin(); i1 != jit1->second.end();)
        {
            int c = i1->first;
            Real v0 = i1->second; i1++; if (i1==jit1->second.end()) break;
            Real v1 = i1->second; i1++; if (i1==jit1->second.end()) break;
            Real v2 = i1->second; i1++;
            internalData.JR.set(l,c+0,v0 * R[(c+0)*3+0] + v1 * R[(c+1)*3+0] + v2 * R[(c+1)*3+0] );
            internalData.JR.set(l,c+1,v0 * R[(c+0)*3+1] + v1 * R[(c+1)*3+1] + v2 * R[(c+2)*3+1] );
            internalData.JR.set(l,c+2,v0 * R[(c+0)*3+2] + v1 * R[(c+1)*3+2] + v2 * R[(c+3)*3+2] );
        }
        nl++;
    }

    internalData.JRMinv.clear();
    internalData.JRMinv.resize(nl,this->currentGroup->systemMatrix->rowSize());

    //compute JRMinv = JR * Minv

    nl = 0;
    for (typename SparseMatrix<Real>::LineConstIterator jit1 = internalData.JR.begin(); jit1 != internalData.JR.end(); jit1++)
    {
        for (unsigned c = 0; c<this->currentGroup->systemMatrix->rowSize(); c++)
        {
            Real v = 0.0;
            for (typename SparseMatrix<Real>::LElementConstIterator i1 = jit1->second.begin(); i1 != jit1->second.end(); i1++)
            {
                v += this->currentGroup->systemMatrix->element(i1->first,c) * i1->second;
            }
            internalData.JRMinv.add(nl,c,v);
        }
        nl++;
    }

    //compute Result = JRMinv * Jt

    nl = 0;
    for (typename SparseMatrix<Real>::LineConstIterator jit1 = internalData.JR.begin(); jit1 != internalData.JR.end(); jit1++)
    {
        int l = jit1->first;
        for (typename SparseMatrix<Real>::LineConstIterator jit2 = internalData.JR.begin(); jit2 != internalData.JR.end(); jit2++)
        {
            int c = jit2->first;
            Real res = 0.0;
            for (typename SparseMatrix<Real>::LElementConstIterator i1 = jit2->second.begin(); i1 != jit2->second.end(); i1++)
            {
                res += internalData.JRMinv.element(nl,i1->first) * i1->second;
            }
            result->add(l,c,res*fact);
        }
        nl++;
    }
}

} // namespace linearsolver

} // namespace component

} // namespace sofa

#endif
