/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <electronic/Dump.h>
#include <electronic/Blip.h>
#include <electronic/Everything.h>
#include <electronic/ColumnBundle.h>
#include <electronic/matrix.h>
#include <electronic/SpeciesInfo.h>
#include <electronic/operators.h>
#include <core/Thread.h>
#include <core/Operators.h>
#include <core/DataIO.h>
#include <config.h>
#include <map>

int nAtomsTot(const IonInfo& iInfo)
{	unsigned res=0;
	for(auto sp: iInfo.species) res += sp->atpos.size();
	return res;
}


inline void printGvec(const GridInfo& gInfo, const Basis& b, int i, ofstream& ofs)
{	vector3<> Gvec = b.iGarr[i] * gInfo.G;
	ofs << "  " << Gvec[0] << " " << Gvec[1] << " " << Gvec[2] << "\n";
}


void Dump::dumpQMC()
{
	const IonInfo &iInfo = e->iInfo;
	const ElecInfo &eInfo = e->eInfo;
	const ElecVars &eVars = e->eVars;
	const Energies &ener = e->ener;
	const Basis& basis = e->basis[0];
	const GridInfo& gInfo = e->gInfo;

	BlipConverter blipConvert(gInfo.S);
	string fname; ofstream ofs;
	
	DataGptr nTilde = J(eVars.get_nTot());
	
	//Total effective potential on electrons due to fluid:
	DataRptr Vdiel = eVars.fluidParams.fluidType==FluidNone ? 0 : I(eVars.d_fluid + eVars.V_cavity);
	nullToZero(Vdiel, gInfo);
	
	//Correction term "Atilde_diel" for QMC:
	const double& A_diel = e->ener.E["A_diel"];
												  //Legendre transform to get final correction
	double Atilde_diel = A_diel - dot(nTilde,O(J(Vdiel)));
	logPrintf("QMC energy correction:\n\tA_diel      = %20.12le\n\tAtilde_diel = %20.12le\n", A_diel, Atilde_diel);

	//-------------------------------------------------------------------------------------------
	//Output potential directly in BLIP-function basis (cubic B-splines)
	DataRptr VdielBlip(blipConvert(Vdiel));

	#define StartDump(varName) \
		fname = getFilename(varName); \
		logPrintf("Dumping '%s'... ", fname.c_str()); logFlush(); \
		if(!mpiUtil->isHead()) fname = "/dev/null";
	StartDump("expot.data")
	ofs.open(fname);
	ofs.precision(12);
	ofs.setf(std::ios::scientific);
	ofs <<
		"START HEADER\n"
		" CASINO Blip external potential exported by " PACKAGE_NAME " " VERSION_MAJOR_MINOR_PATCH "\n"
		"END HEADER\n"
		"\n"
		"START VERSION\n"
		" 2\n"
		"END VERSION\n"
		"\n"
		"START EXPOT\n"
		" Title\n"
		"  Potential due to fluid in JDFT\n"
		" Number of sets\n"
		"  1\n"
		" START SET 1\n"
		"  Particle types affected\n"
		"   All\n"
		"  Periodicity\n"
		"   PERIODIC\n"
		"  Type of representation\n"
		"   BLIP_PERIODIC\n"
		"  Direction\n"
		"   X\n"
		"  Number of such potentials to add\n"
		"   1\n"
		"  Origin for potential\n"
		"   0.0 0.0 0.0\n"
		"  Potential\n"
		"   Blip grid size\n"
		"    " << gInfo.S[0] << " " << gInfo.S[1] << " " << gInfo.S[2] << "\n"
		"   Blip coefficients\n";
	double* VdielBlipData = VdielBlip->data();
	for(int i=0; i<gInfo.nr; i++) ofs << VdielBlipData[i] << "\n";
	ofs <<
		" END SET 1\n"
		"END EXPOT\n";
	ofs.close();
	logPrintf("done.\n"); logFlush();

	//Output Vexternal in BLIPs (if present)
	for(unsigned s=0; s<eVars.Vexternal.size(); s++)
	{	string varName = "VexternalBlip";
		if(eVars.n.size()==2)
			varName += s==0 ? "Up" : "Dn";
		fname = getFilename(varName);
		logPrintf("Dumping '%s'...", fname.c_str()); logFlush();
		if(mpiUtil->isHead()) saveRawBinary(blipConvert(eVars.Vexternal[s]), fname.c_str());
		logPrintf("done.\n"); logFlush();
	}
	
	//-------------------------------------------------------------------------------------------
	//Output wavefunctions directly in BLIP-function basis (cubic B-splines)
	StartDump("bwfn.data")
	logPrintf("\n");
	ofs.open(fname);
	ofs.precision(12);
	ofs.setf(std::ios::scientific);
	ofs <<
		"CASINO Blip orbitals exported by  " PACKAGE_NAME "\n"
		"\n"
		"BASIC INFO\n"
		"----------\n"
		" Generated by:\n"
		"  " PACKAGE_NAME " " VERSION_MAJOR_MINOR_PATCH "\n"
		" Method:\n"
		"  DFT\n"
		" DFT Functional:\n"
		"  " << e->exCorr.getName() << "\n"
		" Pseudopotential\n"
		"  Norm-conserving Kleinman-Bylander\n"
		" Plane wave cutoff (au)\n"
		"  " << e->cntrl.Ecut << "\n"
		" Spin polarized:\n"
		"  " << (eInfo.spinType==SpinNone ? 'F' : 'T') << "\n"
		" Total energy (au per primitive cell)\n"
		"  " << ener.E["Etot"] << "\n"
		" Kinetic energy (au per primitive cell)\n"
		"  " << ener.E["KE"] << "\n"
		" Local potential energy (au per primitive cell)\n"
		"  " << ener.E["Eloc"] + A_diel - Atilde_diel << "\n"
		" Non local potential energy (au per primitive cell)\n"
		"  " << ener.E["Enl"] + ener.E["Exc"] << "\n"
		" Electron electron energy (au per primitive cell)\n"
		"  " << ener.E["EH"] << "\n"
		" Ion-ion energy (au per primitive cell)\n"
		"  " << ener.E["Eewald"] + Atilde_diel << "\n"
		" Number of electrons per primitive cell\n"
		"  " << int(eInfo.nElectrons+0.5) << "\n"
		"\n"
		"GEOMETRY\n"
		"--------\n"
		" Number of atoms per primitive cell\n"
		"  " << nAtomsTot(e->iInfo) << "\n"
		" Atomic number and position of the atoms(au)\n";
	for(auto sp: iInfo.species)
		for(unsigned a=0; a<sp->atpos.size(); a++)
		{
			assert(sp->atomicNumber);
			vector3<> coord(gInfo.R * sp->atpos[a]);
			ofs << "  " << sp->atomicNumber << " "
				<< coord[0] << " " << coord[1] << " " << coord[2]
				<< "\n";
		}
	ofs <<
		" Primitive lattice vectors (au)\n"
		"  " << gInfo.R(0,0) << " " << gInfo.R(1,0) << " " << gInfo.R(2,0) << "\n"
		"  " << gInfo.R(0,1) << " " << gInfo.R(1,1) << " " << gInfo.R(2,1) << "\n"
		"  " << gInfo.R(0,2) << " " << gInfo.R(1,2) << " " << gInfo.R(2,2) << "\n"
		"\n"
		"G VECTORS\n"
		"---------\n"
		" Number of G-vectors\n"
		"  " << basis.nbasis << "\n"
		" Gx Gy Gz (au)\n";
	size_t iG0 = 0;								  //index of the G=0 component
	for(size_t i=0; i<basis.nbasis; i++)
		if(basis.iGarr[i]==vector3<int>(0,0,0))
			iG0=i;
	printGvec(gInfo, basis, iG0, ofs);
	for(size_t i=0; i<basis.nbasis; i++)
		if(i!=iG0) printGvec(gInfo, basis, i, ofs);
	ofs <<
		" Blip Grid\n"
		"  " << gInfo.S[0] << " " << gInfo.S[1] << " " << gInfo.S[2] << "\n"
		"\n"
		"WAVE FUNCTION\n"
		"-------------\n";
	int nSpins = (eInfo.spinType==SpinNone ? 1 : 2);
	int nkPoints = eInfo.nStates / nSpins;
	ofs <<
		" Number of k-points\n"
		"  " << nkPoints << "\n";

	for(int ik=0; ik<nkPoints; ik++)
	{
		vector3<> k(eInfo.qnums[ik].k * gInfo.G); //cartesian k-vector
		ofs <<
			" k-point # ; # of bands (up spin/down spin) ; k-point coords (au)\n"
			"  " << ik+1 << " " << eInfo.nBands << " " << (nSpins==2 ? eInfo.nBands : 0)
			<< " " << k[0] << " " << k[1] << " " << k[2] << "\n";
		for(int s=0; s<nSpins; s++)
		{
			int spinIndex = 1-2*s;
			int q = ik + nkPoints*s; //net quantum number
			
			//Get relevant wavefunctions and eigenvalues (from another process if necessary)
			ColumnBundle CqTemp; diagMatrix Hsub_eigsqTemp;
			const ColumnBundle* Cq=0; const diagMatrix* Hsub_eigsq=0;
			if(mpiUtil->isHead())
			{	if(eInfo.isMine(q))
				{	Cq = &eVars.C[q];
					Hsub_eigsq = &eVars.Hsub_eigs[q];
				}
				else
				{	Cq = &CqTemp;
					CqTemp.init(eInfo.nBands, e->basis[q].nbasis, &e->basis[q], &eInfo.qnums[q]);
					CqTemp.recv(eInfo.whose(q));
					Hsub_eigsq = &Hsub_eigsqTemp;
					Hsub_eigsqTemp.resize(eInfo.nBands);
					Hsub_eigsqTemp.recv(eInfo.whose(q));
				}
			}
			else
			{	if(eInfo.isMine(q))
				{	eVars.C[q].send(0);
					eVars.Hsub_eigs[q].send(0);
				}
				continue; //only head performs computation below
			}
			
			//Loop over bands
			for (int b=0; b < eInfo.nBands; b++)
			{
				logPrintf("\tProcessing state %3d band %3d:\n", q, b); logFlush();
				int bandIndex = b+1 + s*eInfo.nBands;
				ofs <<
					" Band, spin, eigenvalue (au), localized\n"
					"  " << bandIndex << " " << spinIndex << " " << Hsub_eigsq->at(b) << " F\n"
					" " << (nkPoints==1 ? "Real" : "Complex") << " blip coefficients for extended orbitals\n";
				//Get orbital in real space
				complexDataRptr phi = I(Cq->getColumn(b));
				//Compute kinetic and potential energy (in Vdiel) of original PW orbitals:
				double Tpw = -0.5*dot(phi, Jdag(L(J(phi)))).real();
				double Vpw = gInfo.dV*dot(phi, Vdiel*phi).real();
				//Adjust phase, convert to blip and output in appropriate format:
				if(nkPoints==1)
				{	//Convert orbital to real:
					double phaseMean, phaseSigma, imagErrorRMS;
					removePhase(gInfo.nr, phi->data(), phaseMean, phaseSigma, imagErrorRMS);
					logPrintf("\t\tPhase = %lf +/- %lf\n", phaseMean, phaseSigma); logFlush();
					logPrintf("\t\tImagErrorRMS = %le\n", imagErrorRMS); logFlush();
					//Convert PW to blip and output:
					phi = blipConvert(phi);
					complex* phiData = phi->data();
					for(int i=0; i<gInfo.nr; i++)
						ofs << "  " << phiData[i].real() << "\n";
				}
				else
				{	//Convert PW to blip:
					phi = blipConvert(phi); complex* phiData = phi->data();
					//Output:
					for(int i=0; i<gInfo.nr; i++)
						ofs << "  (" << phiData[i].real() << "," << phiData[i].imag() << ")\n";
				}
				//Compare PW and Blip kinetic and potential energies
				double tMax; int i0max, i1max, i2max;
				double Tblip = ::Tblip(phi, &tMax, &i0max, &i1max, &i2max);
				logPrintf("\t\tKinetic Energy    (PW)    = %.12le\n", Tpw);
				logPrintf("\t\tKinetic Energy    (Blip)  = %.12le (Ratio = %.6lf)\n", Tblip, Tblip/Tpw);
				logPrintf("\t\tMax local KE      (Blip)  = %.12le at cell#(%d,%d,%d)\n", tMax, i0max, i1max, i2max);
				logFlush();
				if(A_diel)
				{	//Vdiel potential contribution only when Adiel is non-zero
					double Vblip = ::Vblip(phi, VdielBlip);
					logPrintf("\t\tInt Vdiel.|phi^2| (PW)    = %.12le\n", Vpw);
					logPrintf("\t\tInt Vdiel.|phi^2| (Blip)  = %.12le (Ratio = %.6lf)\n", Vblip, Vblip/Vpw);
					logFlush();
				}
			}
		}
	}
	logPrintf("\tDone.\n"); logFlush();
	ofs.close();
}
