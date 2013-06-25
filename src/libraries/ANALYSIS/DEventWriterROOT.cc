#include "DEventWriterROOT.h"

unsigned int dInitNumThrownArraySize = 100;
unsigned int dInitNumUnusedArraySize = 100;
unsigned int gNumEventWriterThreads = 0;
string dThrownTreeFileName = "";

map<TTree*, unsigned int>* dNumThrownArraySizeMap = NULL;
map<TTree*, unsigned int>* dNumUnusedArraySizeMap = NULL;
map<string, map<string, TClonesArray*> >* dClonesArrayMap = NULL; //first key is tree name, 2nd key is branch name
map<string, map<string, TObject*> >* dTObjectMap = NULL; //first key is tree name, 2nd key is branch name
deque<TFile*>* dOutputROOTFiles = NULL;

DEventWriterROOT::DEventWriterROOT(JEventLoop* locEventLoop)
{
	japp->RootWriteLock();
	{
		++gNumEventWriterThreads;
		if(dNumThrownArraySizeMap != NULL)
		{
			//objects already created
			japp->RootUnLock();
			return;
		}
		dNumUnusedArraySizeMap = new map<TTree*, unsigned int>();
		dNumThrownArraySizeMap = new map<TTree*, unsigned int>();
		dOutputROOTFiles = new deque<TFile*>();
		dClonesArrayMap = new map<string, map<string, TClonesArray*> >();
		dTObjectMap = new map<string, map<string, TObject*> >();
	}
	japp->RootUnLock();
}

DEventWriterROOT::~DEventWriterROOT(void)
{
	japp->RootWriteLock();
	{
		--gNumEventWriterThreads;
		if(gNumEventWriterThreads == 0)
		{
			for(size_t loc_i = 0; loc_i < dOutputROOTFiles->size(); ++loc_i)
			{
				(*dOutputROOTFiles)[loc_i]->Write();
				(*dOutputROOTFiles)[loc_i]->Close();
				delete (*dOutputROOTFiles)[loc_i];
			}
			if(dNumUnusedArraySizeMap != NULL)
			{
				delete dNumUnusedArraySizeMap;
				dNumUnusedArraySizeMap = NULL;
			}
			if(dNumThrownArraySizeMap != NULL)
			{
				delete dNumThrownArraySizeMap;
				dNumThrownArraySizeMap = NULL;
			}
			if(dOutputROOTFiles != NULL)
			{
				delete dOutputROOTFiles;
				dOutputROOTFiles = NULL;
			}
			if(dClonesArrayMap != NULL)
			{
				delete dClonesArrayMap;
				dClonesArrayMap = NULL;
			}
			if(dTObjectMap != NULL)
			{
				delete dTObjectMap;
				dTObjectMap = NULL;
			}
		}
	}
	japp->RootUnLock();
}

void DEventWriterROOT::Create_ThrownTree(string locOutputFileName) const
{
	japp->RootWriteLock();
	{
		if(dThrownTreeFileName != "") //tree already created
		{
			japp->RootUnLock();
			return; //already created by another thread
		}
		dThrownTreeFileName = locOutputFileName;

		//create ROOT file if it doesn't exist already
		TFile* locFile = (TFile*)gROOT->FindObject(dThrownTreeFileName.c_str());
		if(locFile == NULL)
		{
			locFile = new TFile(dThrownTreeFileName.c_str(), "RECREATE");
			dOutputROOTFiles->push_back(locFile);
		}

		//create tree if it doesn't exist already
		string locTreeName = "Thrown_Tree";
		locFile->cd();
		if(gDirectory->Get(locTreeName.c_str()) != NULL)
		{
			japp->RootUnLock();
			return; //already created by another thread
		}
		TTree* locTree = new TTree(locTreeName.c_str(), locTreeName.c_str());

		dNumUnusedArraySizeMap->insert(pair<TTree*, unsigned int>(locTree, 0));
		dNumThrownArraySizeMap->insert(pair<TTree*, unsigned int>(locTree, dInitNumThrownArraySize));

	/******************************************************************** Create Branches ********************************************************************/

		//create basic/misc. tree branches (run#, event#, etc.)
		Create_Branch_Fundamental<UInt_t>(locTree, "", "RunNumber", "i");
		Create_Branch_Fundamental<UInt_t>(locTree, "", "EventNumber", "i");
		Create_Branch_Fundamental<Double_t>(locTree, "", "RFTime_Thrown", "D");

		//create thrown particle branches
		string locNumThrownString = "NumThrown";
		Create_Branch_Fundamental<ULong64_t>(locTree, "", "NumPIDThrown_FinalState", "l"); //19 digits
		Create_Branch_Fundamental<ULong64_t>(locTree, "", "PIDThrown_Decaying", "l");
		Create_Branch_Fundamental<UInt_t>(locTree, "", locNumThrownString, "i");
		Create_Branches_ThrownParticle(locTree, "Thrown", locNumThrownString, true);
	}
	japp->RootUnLock();
}

void DEventWriterROOT::Create_DataTrees(JEventLoop* locEventLoop) const
{
	vector<const DMCThrown*> locMCThrowns;
	locEventLoop->Get(locMCThrowns);

	vector<const DReaction*> locReactions;
	Get_Reactions(locEventLoop, locReactions);

	japp->RootWriteLock();
	{
		for(size_t loc_i = 0; loc_i < locReactions.size(); ++loc_i)
		{
			if(locReactions[loc_i]->Get_EnableTTreeOutputFlag())
				Create_DataTree(locReactions[loc_i], !locMCThrowns.empty());
		}
	}
	japp->RootUnLock();
}

void DEventWriterROOT::Create_DataTree(const DReaction* locReaction, bool locIsMCDataFlag) const
{
	string locReactionName = locReaction->Get_ReactionName();

	//create ROOT file if it doesn't exist already
	string locOutputFileName = locReaction->Get_TTreeOutputFileName();
	TFile* locFile = (TFile*)gROOT->FindObject(locOutputFileName.c_str());
	if(locFile == NULL)
	{
		locFile = new TFile(locOutputFileName.c_str(), "RECREATE");
		dOutputROOTFiles->push_back(locFile);
	}

	//create tree if it doesn't exist already
	string locTreeName = locReactionName + string("_Tree");
	locFile->cd();
	if(gDirectory->Get(locTreeName.c_str()) != NULL)
		return; //already created by another thread
	TTree* locTree = new TTree(locTreeName.c_str(), locTreeName.c_str());

	dNumUnusedArraySizeMap->insert(pair<TTree*, unsigned int>(locTree, dInitNumUnusedArraySize));
	dNumThrownArraySizeMap->insert(pair<TTree*, unsigned int>(locTree, dInitNumThrownArraySize));

	bool locKinFitFlag = (locReaction->Get_KinFitType() != d_NoFit);

	//create & add reaction identification maps
	TList* locUserInfo = locTree->GetUserInfo();
	TMap* locNameToPIDMap = new TMap();
	locNameToPIDMap->SetName("NameToPIDMap");
	locUserInfo->Add(locNameToPIDMap);

	TMap* locNameToPositionMap = new TMap(); //not filled for target or initial particles
	locNameToPositionMap->SetName("NameToPositionMap");
	locUserInfo->Add(locNameToPositionMap);

	TMap* locPositionToNameMap = new TMap(); //not filled for target or initial particles
	locPositionToNameMap->SetName("PositionToNameMap");
	locUserInfo->Add(locPositionToNameMap);

	TMap* locPositionToPIDMap = new TMap();
	locPositionToPIDMap->SetName("PositionToPIDMap");
	locUserInfo->Add(locPositionToPIDMap);

	TMap* locMiscInfoMap = new TMap();
	locMiscInfoMap->SetName("MiscInfoMap");
	locUserInfo->Add(locMiscInfoMap);

	ostringstream locKinFitTypeStream;
	locKinFitTypeStream << (unsigned int)locReaction->Get_KinFitType();
	locMiscInfoMap->Add(new TObjString("KinFitType"), new TObjString(locKinFitTypeStream.str().c_str()));

	//find the # particles of each pid
	map<Particle_t, unsigned int> locParticleNumberMap;
	for(size_t loc_i = 0; loc_i < locReaction->Get_NumReactionSteps(); ++loc_i)
	{
		const DReactionStep* locReactionStep = locReaction->Get_ReactionStep(loc_i);
		deque<Particle_t> locFinalParticleIDs;
		locReactionStep->Get_FinalParticleIDs(locFinalParticleIDs);
		for(size_t loc_j = 0; loc_j < locFinalParticleIDs.size(); ++loc_j)
		{
			if(locReactionStep->Get_MissingParticleIndex() == int(loc_j)) //missing particle
				continue;
			Particle_t locPID = locFinalParticleIDs[loc_j];
			if(locParticleNumberMap.find(locPID) == locParticleNumberMap.end())
				locParticleNumberMap[locPID] = 1;
			else
				++locParticleNumberMap[locPID];
		}
	}

	//fill maps
	map<Particle_t, unsigned int> locParticleNumberMap_Current;
	Particle_t locPID;
	ostringstream locPIDStream, locPositionStream, locParticleNameStream;
	TObjString *locObjString_PID, *locObjString_Position, *locObjString_ParticleName;
	for(size_t loc_i = 0; loc_i < locReaction->Get_NumReactionSteps(); ++loc_i)
	{
		const DReactionStep* locReactionStep = locReaction->Get_ReactionStep(loc_i);

		//initial particle
		locPID = locReactionStep->Get_InitialParticleID();
		locPIDStream.str("");
		locPIDStream << (unsigned int)locPID;
		locObjString_PID = new TObjString(locPIDStream.str().c_str());
		locPositionStream.str("");
		locPositionStream << loc_i << "_" << -1;
		locObjString_Position = new TObjString(locPositionStream.str().c_str());
		locPositionToPIDMap->Add(locObjString_Position, locObjString_PID);

		//target particle
		locPID = locReactionStep->Get_TargetParticleID();
		if(locPID != Unknown)
		{
			locPIDStream.str("");
			locPIDStream << (unsigned int)locPID;
			locObjString_PID = new TObjString(locPIDStream.str().c_str());
			locPositionStream.str("");
			locPositionStream << loc_i << "_" << -2;
			locObjString_Position = new TObjString(locPositionStream.str().c_str());
			locPositionToPIDMap->Add(locObjString_Position, locObjString_PID);
			locObjString_ParticleName = new TObjString("Target"); //assumes there is only one!!
			locNameToPositionMap->Add(locObjString_ParticleName, locObjString_Position);
			locNameToPIDMap->Add(locObjString_ParticleName, locObjString_PID);
			locMiscInfoMap->Add(locObjString_ParticleName, locObjString_PID);
		}

		//final particles
		deque<Particle_t> locFinalParticleIDs;
		locReactionStep->Get_FinalParticleIDs(locFinalParticleIDs);
		for(size_t loc_j = 0; loc_j < locFinalParticleIDs.size(); ++loc_j)
		{
			locPID = locFinalParticleIDs[loc_j];
			locPIDStream.str("");
			locPIDStream << (unsigned int)locPID;
			locObjString_PID = new TObjString(locPIDStream.str().c_str());

			locPositionStream.str("");
			locPositionStream << loc_i << "_" << loc_j;
			locObjString_Position = new TObjString(locPositionStream.str().c_str());

			if(locReactionStep->Get_MissingParticleIndex() == int(loc_j)) //missing particle
			{
				locObjString_ParticleName = new TObjString("Missing");
				locNameToPositionMap->Add(locObjString_ParticleName, locObjString_Position);
				locNameToPIDMap->Add(locObjString_ParticleName, locObjString_PID);
				locMiscInfoMap->Add(locObjString_ParticleName, locObjString_PID);
				continue;
			}

			if(locParticleNumberMap_Current.find(locPID) == locParticleNumberMap_Current.end())
				locParticleNumberMap_Current[locPID] = 1;
			else
				++locParticleNumberMap_Current[locPID];

			locParticleNameStream.str("");
			locParticleNameStream << Convert_ToBranchName(ParticleType(locPID));
			if(locParticleNumberMap[locPID] > 1)
				locParticleNameStream << locParticleNumberMap_Current[locPID];
			locObjString_ParticleName = new TObjString(locParticleNameStream.str().c_str());

			locPositionToPIDMap->Add(locObjString_Position, locObjString_PID);
			locNameToPositionMap->Add(locObjString_ParticleName, locObjString_Position);
			locPositionToNameMap->Add(locObjString_Position, locObjString_ParticleName);
			locNameToPIDMap->Add(locObjString_ParticleName, locObjString_PID);
		}
	}

/******************************************************************** Create Branches ********************************************************************/

	//create basic/misc. tree branches (run#, event#, etc.)
	Create_Branch_Fundamental<UInt_t>(locTree, "", "RunNumber", "i");
	Create_Branch_Fundamental<UInt_t>(locTree, "", "EventNumber", "i");
	if(locIsMCDataFlag)
		Create_Branch_Fundamental<Double_t>(locTree, "", "RFTime_Thrown", "D");

	//create combo-dependent, particle-independent branches
	Create_Branch_Fundamental<Double_t>(locTree, "", "RFTime_Measured", "D");
	if(locKinFitFlag)
	{
		Create_Branch_Fundamental<Double_t>(locTree, "", "ChiSq_KinFit", "D");
		Create_Branch_Fundamental<UInt_t>(locTree, "", "NDF_KinFit", "i");
		Create_Branch_Fundamental<Double_t>(locTree, "", "RFTime_KinFit", "D");
	}

	//create branches for spacetime vertices
	for(size_t loc_i = 0; loc_i < locReaction->Get_NumReactionSteps(); ++loc_i)
	{
		const DReactionStep* locReactionStep = locReaction->Get_ReactionStep(loc_i);
		locPID = locReactionStep->Get_InitialParticleID();
		if((locPID == Gamma) || (locPID == Electron) || (locPID == Positron)) //beam
			Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "", "X4_Production", (*dTObjectMap)[locTree->GetName()]);
		else if(IsDetachedVertex(locPID))
		{
			string locVariableName = string("X4_") + Convert_ToBranchName(ParticleType(locPID)) + string("Decay");
			Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "", locVariableName, (*dTObjectMap)[locTree->GetName()]);
		}
	}

	//create thrown particle branches
	if(locIsMCDataFlag)
	{
		string locNumThrownString = "NumThrown";
		Create_Branch_Fundamental<UInt_t>(locTree, "", locNumThrownString, "i");
		Create_Branches_ThrownParticle(locTree, "Thrown", locNumThrownString, false);
	}

	//create branches for particles
	locParticleNumberMap_Current.clear();
	for(size_t loc_i = 0; loc_i < locReaction->Get_NumReactionSteps(); ++loc_i)
	{
		const DReactionStep* locReactionStep = locReaction->Get_ReactionStep(loc_i);
		locPID = locReactionStep->Get_InitialParticleID();
		//should check to make sure the beam particle isn't missing...
		if((locPID == Gamma) || (locPID == Electron) || (locPID == Positron))
			Create_Branches_Beam(locTree, locKinFitFlag);

		//final particles
		deque<Particle_t> locFinalParticleIDs;
		locReactionStep->Get_FinalParticleIDs(locFinalParticleIDs);
		for(size_t loc_j = 0; loc_j < locFinalParticleIDs.size(); ++loc_j)
		{
			locPID = locFinalParticleIDs[loc_j];
			if(locParticleNumberMap_Current.find(locPID) == locParticleNumberMap_Current.end())
				locParticleNumberMap_Current[locPID] = 1;
			else
				++locParticleNumberMap_Current[locPID];

			locParticleNameStream.str("");
			locParticleNameStream << Convert_ToBranchName(ParticleType(locPID));
			if(locParticleNumberMap[locPID] > 1)
				locParticleNameStream << locParticleNumberMap_Current[locPID];
			locObjString_ParticleName = new TObjString(locParticleNameStream.str().c_str());

			if(locReaction->Check_IsDecayingParticle(locFinalParticleIDs[loc_j], loc_i + 1))
				continue; //decaying particle
			if(locReactionStep->Get_MissingParticleIndex() == int(loc_j))
				continue; //missing particle
			Create_Branches_FinalStateParticle(locTree, locParticleNameStream.str(), (ParticleCharge(locPID) != 0), locKinFitFlag, locIsMCDataFlag);
		}
	}

	//create unused particle branches
	string locNumUnusedString = "NumUnused";
	Create_Branch_Fundamental<UInt_t>(locTree, "", locNumUnusedString, "i");
	Create_Branches_UnusedParticle(locTree, "Unused", locNumUnusedString, locIsMCDataFlag);
}

void DEventWriterROOT::Create_Branches_FinalStateParticle(TTree* locTree, string locParticleBranchName, bool locIsChargedFlag, bool locKinFitFlag, bool locIsMCDataFlag) const
{
	//IDENTIFIER / MATCHING
	Create_Branch_Fundamental<Int_t>(locTree, locParticleBranchName, "ObjectID", "I");
	if(locIsMCDataFlag)
		Create_Branch_Fundamental<Int_t>(locTree, locParticleBranchName, "MatchID", "I");

	//KINEMATICS: MEASURED //at the production vertex
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "X4_Measured", (*dTObjectMap)[locTree->GetName()]);
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "P4_Measured", (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: END
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "X4_End", (*dTObjectMap)[locTree->GetName()]);
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "P4_End", (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: KINFIT //at the production vertex
	if(locKinFitFlag)
	{
		Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "X4_KinFit", (*dTObjectMap)[locTree->GetName()]);
		Create_Branch_NoSplitTObject<TLorentzVector>(locTree, locParticleBranchName, "P4_KinFit", (*dTObjectMap)[locTree->GetName()]);
	}

	//KINEMATICS: OTHER
	Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "PathLength", "D");

	//PID QUALITY
	Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_Measured", "D");
	if(locKinFitFlag)
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_KinFit", "D");
	Create_Branch_Fundamental<UInt_t>(locTree, locParticleBranchName, "NDF_Timing", "i");
	if(locIsChargedFlag)
	{
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "ChiSq_Tracking", "D");
		Create_Branch_Fundamental<UInt_t>(locTree, locParticleBranchName, "NDF_Tracking", "i");
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "ChiSq_DCdEdx", "D");
		Create_Branch_Fundamental<UInt_t>(locTree, locParticleBranchName, "NDF_DCdEdx", "i");
	}

	//DEPOSITED ENERGY
	if(locIsChargedFlag)
	{
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "dEdx_CDC", "D");
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "dEdx_FDC", "D");
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "dEdx_TOF", "D");
		Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "dEdx_ST", "D");
	}
	Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "Energy_BCAL", "D");
	Create_Branch_Fundamental<Double_t>(locTree, locParticleBranchName, "Energy_FCAL", "D");
}

void DEventWriterROOT::Create_Branches_Beam(TTree* locTree, bool locKinFitFlag) const
{
	//IDENTIFIER
	Create_Branch_Fundamental<Int_t>(locTree, "Beam", "ObjectID", "I");

	//KINEMATICS: MEASURED //at the target center
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "Beam", "X4_Measured", (*dTObjectMap)[locTree->GetName()]);
	Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "Beam", "P4_Measured", (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: KINFIT //at the interaction vertex
	if(locKinFitFlag)
	{
		Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "Beam", "X4_KinFit", (*dTObjectMap)[locTree->GetName()]);
		Create_Branch_NoSplitTObject<TLorentzVector>(locTree, "Beam", "P4_KinFit", (*dTObjectMap)[locTree->GetName()]);
	}
}

void DEventWriterROOT::Create_Branches_UnusedParticle(TTree* locTree, string locParticleBranchName, string locArraySizeString, bool locIsMCDataFlag) const
{
	//IDENTIFIERS / MATCHING
	Create_Branch_FundamentalArray<Int_t>(locTree, locParticleBranchName, "ObjectID", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "I");
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "PID", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "i");
	if(locIsMCDataFlag)
		Create_Branch_FundamentalArray<Int_t>(locTree, locParticleBranchName, "MatchID", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "I");

	//KINEMATICS: MEASURED //at the production vertex
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "X4_Measured", "TLorentzVector", (*dNumUnusedArraySizeMap)[locTree]);
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "P4_Measured", "TLorentzVector", (*dNumUnusedArraySizeMap)[locTree]);

	//KINEMATICS: END
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "X4_End", "TLorentzVector", (*dNumUnusedArraySizeMap)[locTree]);
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "P4_End", "TLorentzVector", (*dNumUnusedArraySizeMap)[locTree]);

	//KINEMATICS: OTHER
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "PathLength", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");

	//PID QUALITY
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "ChiSq_Tracking", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "NDF_Tracking", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "i");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "NDF_Timing", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "i");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "ChiSq_DCdEdx", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "NDF_DCdEdx", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "i");

	//DEPOSITED ENERGY
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "dEdx_CDC", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "dEdx_FDC", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "dEdx_TOF", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "dEdx_ST", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "Energy_BCAL", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
	Create_Branch_FundamentalArray<Double_t>(locTree, locParticleBranchName, "Energy_FCAL", locArraySizeString, (*dNumUnusedArraySizeMap)[locTree], "D");
}

void DEventWriterROOT::Create_Branches_ThrownParticle(TTree* locTree, string locParticleBranchName, string locArraySizeString, bool locIsOnlyThrownFlag) const
{
	//IDENTIFIERS
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "ObjectID", locArraySizeString, (*dNumThrownArraySizeMap)[locTree], "i");
	Create_Branch_FundamentalArray<UInt_t>(locTree, locParticleBranchName, "ParentID", locArraySizeString, (*dNumThrownArraySizeMap)[locTree], "i");
	Create_Branch_FundamentalArray<Int_t>(locTree, locParticleBranchName, "PID_PDG", locArraySizeString, (*dNumThrownArraySizeMap)[locTree], "I");
	if(!locIsOnlyThrownFlag)
		Create_Branch_FundamentalArray<Int_t>(locTree, locParticleBranchName, "MatchID", locArraySizeString, (*dNumThrownArraySizeMap)[locTree], "I");

	//THROWN PARTICLES BY PID
	Create_Branch_Fundamental<ULong64_t>(locTree, "", "NumPIDThrown_FinalState", "l"); //19 digits
	Create_Branch_Fundamental<ULong64_t>(locTree, "", "PIDThrown_Decaying", "l");

	//KINEMATICS: THROWN //at the production vertex
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "X4_Thrown", "TLorentzVector", (*dNumThrownArraySizeMap)[locTree]);
	Create_Branch_ClonesArray(locTree, locParticleBranchName, "P4_Thrown", "TLorentzVector", (*dNumThrownArraySizeMap)[locTree]);
}

string DEventWriterROOT::Create_Branch_ClonesArray(TTree* locTree, string locParticleBranchName, string locVariableName, string locClassName, unsigned int locSize) const
{
	string locBranchName = (locParticleBranchName != "") ? locParticleBranchName + string("__") + locVariableName : locVariableName;
	(*dClonesArrayMap)[locTree->GetName()].insert(pair<string, TClonesArray*>(locBranchName, new TClonesArray(locClassName.c_str(), locSize)));
	locTree->Branch(locBranchName.c_str(), &((*dClonesArrayMap)[locTree->GetName()][locBranchName]), 32000, 0); //0: don't split
	return locBranchName;
}

string DEventWriterROOT::Convert_ToBranchName(string locInputName) const
{
	TString locTString(locInputName);
	locTString.ReplaceAll("*", "Star");
	locTString.ReplaceAll("(", "_");
	locTString.ReplaceAll(")", "_");
	locTString.ReplaceAll("+", "Plus");
	locTString.ReplaceAll("-", "Minus");
	locTString.ReplaceAll("'", "Prime");
	return (string)((const char*)locTString);
}

void DEventWriterROOT::Get_Reactions(jana::JEventLoop* locEventLoop, vector<const DReaction*>& locReactions) const
{
	// Get list of factories and find all the ones producing
	// DReaction objects. (A simpler way to do this would be to
	// just use locEventLoop->Get(...), but then only one plugin could
	// be used at a time.)
	locReactions.clear();
	vector<JFactory_base*> locFactories = locEventLoop->GetFactories();
	for(size_t loc_i = 0; loc_i < locFactories.size(); ++loc_i)
	{
		JFactory<DReaction>* locFactory = dynamic_cast<JFactory<DReaction>* >(locFactories[loc_i]);
		if(locFactory == NULL)
			continue;
		if(string(locFactory->Tag()) == "Thrown")
			continue;

		// Found a factory producing DReactions. The reaction objects are
		// produced at the init stage and are persistent through all event
		// processing so we can grab the list here and append it to our
		// overall list.
		vector<const DReaction*> locReactionsSubset;
		locFactory->Get(locReactionsSubset);
		locReactions.insert(locReactions.end(), locReactionsSubset.begin(), locReactionsSubset.end());
	}
}

void DEventWriterROOT::Fill_ThrownTree(JEventLoop* locEventLoop) const
{
	vector<const DMCThrown*> locMCThrowns_FinalState;
	locEventLoop->Get(locMCThrowns_FinalState, "FinalState");

	vector<const DMCThrown*> locMCThrowns_Decaying;
	locEventLoop->Get(locMCThrowns_Decaying, "Decaying");

	vector<const DEventRFBunch*> locThrownEventRFBunches;
	locEventLoop->Get(locThrownEventRFBunches, "Thrown");

	string locTreeName = "Thrown_Tree";
	japp->RootWriteLock();
	{
		//get the tree
		TFile* locFile = (TFile*)gROOT->FindObject(dThrownTreeFileName.c_str());
		locFile->cd("/");
		TTree* locTree = (TTree*)gDirectory->Get(locTreeName.c_str());

		//clear the tclonesarry's
		map<string, TClonesArray*>& locTreeMap_ClonesArray = (*dClonesArrayMap)[locTreeName];
		map<string, TClonesArray*>::iterator locClonesArrayMapIterator;
		for(locClonesArrayMapIterator = locTreeMap_ClonesArray.begin(); locClonesArrayMapIterator != locTreeMap_ClonesArray.end(); ++locClonesArrayMapIterator)
			locClonesArrayMapIterator->second->Clear();

		//primary event info
		Fill_FundamentalData<UInt_t>(locTree, "RunNumber", locEventLoop->GetJEvent().GetRunNumber());
		Fill_FundamentalData<UInt_t>(locTree, "EventNumber", locEventLoop->GetJEvent().GetEventNumber());
		if(!locThrownEventRFBunches.empty())
			Fill_FundamentalData<Double_t>(locTree, "RFTime_Thrown", locThrownEventRFBunches[0]->dTime);

		//throwns
		size_t locNumThrown = locMCThrowns_FinalState.size() + locMCThrowns_Decaying.size();
		if(locNumThrown > 0)
		{
			Fill_FundamentalData<UInt_t>(locTree, "NumThrown", locNumThrown);
			for(size_t loc_j = 0; loc_j < locMCThrowns_FinalState.size(); ++loc_j)
				Fill_ThrownParticleData(locTree, loc_j, locNumThrown, locMCThrowns_FinalState[loc_j]);
			for(size_t loc_j = 0; loc_j < locMCThrowns_Decaying.size(); ++loc_j)
				Fill_ThrownParticleData(locTree, loc_j, locNumThrown, locMCThrowns_Decaying[loc_j]);

			//THROWN PARTICLES BY PID
			ULong64_t locNumPIDThrown_FinalState = 0, locPIDThrown_Decaying = 0;
			for(size_t loc_j = 0; loc_j < locMCThrowns_FinalState.size(); ++loc_j) //final state
			{
				Particle_t locPID = locMCThrowns_FinalState[loc_j]->PID();
				ULong64_t locPIDMultiplexID = Calc_ParticleMultiplexID(locPID);
				unsigned int locCurrentNumParticles = (locNumPIDThrown_FinalState / locPIDMultiplexID) % 10;
				if(locCurrentNumParticles != 9)
					locNumPIDThrown_FinalState += locPIDMultiplexID;
			}
			for(size_t loc_j = 0; loc_j < locMCThrowns_Decaying.size(); ++loc_j) //decaying
			{
				Particle_t locPID = locMCThrowns_Decaying[loc_j]->PID();
				ULong64_t locPIDMultiplexID = Calc_ParticleMultiplexID(locPID);
				if(locPID != Pi0)
					locPIDThrown_Decaying |= locPIDMultiplexID; //bit-wise or
				else //save pi0's as final state instead of decaying
				{
					unsigned int locCurrentNumParticles = (locNumPIDThrown_FinalState / locPIDMultiplexID) % 10;
					if(locCurrentNumParticles != 9)
						locNumPIDThrown_FinalState += locPIDMultiplexID;
				}
			}

			Fill_FundamentalData<ULong64_t>(locTree, "NumPIDThrown_FinalState", locNumPIDThrown_FinalState); //19 digits
			Fill_FundamentalData<ULong64_t>(locTree, "PIDThrown_Decaying", locPIDThrown_Decaying);

			//update array sizes
			if(locNumThrown > (*dNumThrownArraySizeMap)[locTree])
				(*dNumThrownArraySizeMap)[locTree] = locNumThrown;
		}

		locTree->Fill();
	}
	japp->RootUnLock();
}

void DEventWriterROOT::Fill_DataTrees(JEventLoop* locEventLoop, string locDReactionTag) const
{
	if(locDReactionTag == "Thrown")
	{
		cout << "WARNING: CANNOT FILL THROWN TREE WITH THIS FUNCTION." << endl;
		return;
	}

	vector<const DAnalysisResults*> locAnalysisResultsVector;
	locEventLoop->Get(locAnalysisResultsVector);

	vector<const DReaction*> locReactionsWithTag;
	locEventLoop->Get(locReactionsWithTag, locDReactionTag.c_str());

	for(size_t loc_i = 0; loc_i < locAnalysisResultsVector.size(); ++loc_i)
	{
		deque<const DParticleCombo*> locPassedParticleCombos;
		locAnalysisResultsVector[loc_i]->Get_PassedParticleCombos(locPassedParticleCombos);
		if(locPassedParticleCombos.empty())
			continue;
		const DReaction* locReaction = locAnalysisResultsVector[loc_i]->Get_Reaction();
		if(!locReaction->Get_EnableTTreeOutputFlag())
			continue;
		bool locReactionFoundFlag = false;
		for(size_t loc_j = 0; loc_j < locReactionsWithTag.size(); ++loc_j)
		{
			if(locReactionsWithTag[loc_j] != locReaction)
				continue;
			locReactionFoundFlag = true;
			break;
		}
		if(!locReactionFoundFlag)
			continue; //reaction not from this factory, continue
		Fill_DataTree(locEventLoop, locReaction, locPassedParticleCombos);
	}
}

void DEventWriterROOT::Fill_DataTree(JEventLoop* locEventLoop, const DReaction* locReaction, deque<const DParticleCombo*>& locParticleCombos) const
{
	if(locReaction->Get_ReactionName() == "Thrown")
	{
		cout << "WARNING: CANNOT FILL THROWN TREE WITH THIS FUNCTION." << endl;
		return;
	}
	
	if(!locReaction->Get_EnableTTreeOutputFlag())
	{
		cout << "WARNING: ROOT TTREE OUTPUT NOT ENABLED FOR THIS DREACTION (" << locReaction->Get_ReactionName() << ")" << endl;
		return;
	}

	vector<const DMCThrown*> locMCThrowns_FinalState;
	locEventLoop->Get(locMCThrowns_FinalState, "FinalState");

	vector<const DMCThrown*> locMCThrowns_Decaying;
	locEventLoop->Get(locMCThrowns_Decaying, "Decaying");

	vector<const DEventRFBunch*> locThrownEventRFBunches;
	locEventLoop->Get(locThrownEventRFBunches, "Thrown");

	vector<const DChargedTrackHypothesis*> locChargedTrackHypotheses;
	locEventLoop->Get(locChargedTrackHypotheses);

	vector<const DNeutralParticleHypothesis*> locNeutralParticleHypotheses;
	locEventLoop->Get(locNeutralParticleHypotheses);

	const DMCThrownMatching* locMCThrownMatching = NULL;
	locEventLoop->GetSingle(locMCThrownMatching);

	//find max charged identifier #:
	vector<const DChargedTrack*> locChargedTracks;
	locEventLoop->Get(locChargedTracks);
	unsigned long locMaxChargedID = 0;
	for(size_t loc_i = 0; loc_i < locChargedTracks.size(); ++loc_i)
	{
		unsigned long locCandidateID = locChargedTracks[loc_i]->Get_BestFOM()->candidateid;
		if(locCandidateID > locMaxChargedID)
			locMaxChargedID = locCandidateID;
	}

	//create map of neutral shower to new id #
	vector<const DNeutralShower*> locNeutralShowers;
	locEventLoop->Get(locNeutralShowers);
	map<const DNeutralShower*, int> locShowerToIDMap;
	for(size_t loc_i = 0; loc_i < locNeutralShowers.size(); ++loc_i)
		locShowerToIDMap[locNeutralShowers[loc_i]] = locMaxChargedID + 1 + loc_i;

	//create map of beam photon to new id #
	vector<const DBeamPhoton*> locBeamPhotons;
	locEventLoop->Get(locBeamPhotons);
	map<const DBeamPhoton*, int> locBeamToIDMap;
	for(size_t loc_i = 0; loc_i < locBeamPhotons.size(); ++loc_i)
		locBeamToIDMap[locBeamPhotons[loc_i]] = loc_i;

	string locTreeName = locReaction->Get_ReactionName() + string("_Tree");
	japp->RootWriteLock();
	{
		string locOutputFileName = locReaction->Get_TTreeOutputFileName();
		TFile* locFile = (TFile*)gROOT->FindObject(locOutputFileName.c_str());

		//get the tree info
		locFile->cd("/");
		TTree* locTree = (TTree*)gDirectory->Get(locTreeName.c_str());
		TList* locUserInfo = locTree->GetUserInfo();
		TMap* locPositionToNameMap = (TMap*)locUserInfo->FindObject("PositionToNameMap");

		//loop over combos (tree entries)
		for(size_t loc_j = 0; loc_j < locParticleCombos.size(); ++loc_j)
		{
			//clear the tclonesarry's
			map<string, TClonesArray*>& locTreeMap_ClonesArray = (*dClonesArrayMap)[locTreeName];
			map<string, TClonesArray*>::iterator locClonesArrayMapIterator;
			for(locClonesArrayMapIterator = locTreeMap_ClonesArray.begin(); locClonesArrayMapIterator != locTreeMap_ClonesArray.end(); ++locClonesArrayMapIterator)
				locClonesArrayMapIterator->second->Clear();

			//primary event info
			Fill_FundamentalData<UInt_t>(locTree, "RunNumber", locEventLoop->GetJEvent().GetRunNumber());
			Fill_FundamentalData<UInt_t>(locTree, "EventNumber", locEventLoop->GetJEvent().GetEventNumber());
			if(!locThrownEventRFBunches.empty())
				Fill_FundamentalData<Double_t>(locTree, "RFTime_Thrown", locThrownEventRFBunches[0]->dTime);

			//throwns
			size_t locNumThrown = locMCThrowns_FinalState.size() + locMCThrowns_Decaying.size();
			if(locNumThrown > 0)
			{
				Fill_FundamentalData<UInt_t>(locTree, "NumThrown", locNumThrown);
				for(size_t loc_j = 0; loc_j < locMCThrowns_FinalState.size(); ++loc_j)
					Fill_ThrownParticleData(locTree, loc_j, locNumThrown, locMCThrowns_FinalState[loc_j], locMCThrownMatching, locShowerToIDMap);
				for(size_t loc_j = 0; loc_j < locMCThrowns_Decaying.size(); ++loc_j)
					Fill_ThrownParticleData(locTree, loc_j, locNumThrown, locMCThrowns_Decaying[loc_j], locMCThrownMatching, locShowerToIDMap);

				//THROWN PARTICLES BY PID
				ULong64_t locNumPIDThrown_FinalState = 0, locPIDThrown_Decaying = 0;
				for(size_t loc_j = 0; loc_j < locMCThrowns_FinalState.size(); ++loc_j) //final state
				{
					Particle_t locPID = locMCThrowns_FinalState[loc_j]->PID();
					ULong64_t locPIDMultiplexID = Calc_ParticleMultiplexID(locPID);
					unsigned int locCurrentNumParticles = (locNumPIDThrown_FinalState / locPIDMultiplexID) % 10;
					if(locCurrentNumParticles != 9)
						locNumPIDThrown_FinalState += locPIDMultiplexID;
				}
				for(size_t loc_j = 0; loc_j < locMCThrowns_Decaying.size(); ++loc_j) //decaying
				{
					Particle_t locPID = locMCThrowns_Decaying[loc_j]->PID();
					ULong64_t locPIDMultiplexID = Calc_ParticleMultiplexID(locPID);
					if(locPID != Pi0)
						locPIDThrown_Decaying |= locPIDMultiplexID; //bit-wise or
					else //save pi0's as final state instead of decaying
					{
						unsigned int locCurrentNumParticles = (locNumPIDThrown_FinalState / locPIDMultiplexID) % 10;
						if(locCurrentNumParticles != 9)
							locNumPIDThrown_FinalState += locPIDMultiplexID;
					}
				}

				Fill_FundamentalData<ULong64_t>(locTree, "NumPIDThrown_FinalState", locNumPIDThrown_FinalState); //19 digits
				Fill_FundamentalData<ULong64_t>(locTree, "PIDThrown_Decaying", locPIDThrown_Decaying);

				//update array sizes
				if(locNumThrown > (*dNumThrownArraySizeMap)[locTree])
					(*dNumThrownArraySizeMap)[locTree] = locNumThrown;
			}

			//combo data
			const DParticleCombo* locParticleCombo = locParticleCombos[loc_j];
			const DKinFitResults* locKinFitResults = locParticleCombo->Get_KinFitResults();
			const DEventRFBunch* locEventRFBunch = locParticleCombo->Get_EventRFBunch();

			double locRFTime = (locEventRFBunch != NULL) ? locEventRFBunch->dTime : numeric_limits<double>::quiet_NaN();
			Fill_FundamentalData<Double_t>(locTree, "RFTime_Measured", locRFTime);
			if(locKinFitResults != NULL)
			{
				Fill_FundamentalData<Double_t>(locTree, "ChiSq_KinFit", locKinFitResults->Get_ChiSq());
				Fill_FundamentalData<UInt_t>(locTree, "NDF_KinFit", locKinFitResults->Get_NDF());
				double locRFTime_KinFit = (locEventRFBunch != NULL) ? locEventRFBunch->dTime : numeric_limits<double>::quiet_NaN();
				Fill_FundamentalData<Double_t>(locTree, "RFTime_KinFit", locRFTime_KinFit);
			}

			//steps
			vector<const DChargedTrackHypothesis*> locUnusedChargedTrackHypotheses = locChargedTrackHypotheses;
			vector<const DNeutralParticleHypothesis*> locUnusedNeutralParticleHypotheses = locNeutralParticleHypotheses;
			for(size_t loc_k = 0; loc_k < locParticleCombo->Get_NumParticleComboSteps(); ++loc_k)
			{
				const DParticleComboStep* locParticleComboStep = locParticleCombo->Get_ParticleComboStep(loc_k);
				DLorentzVector locStepX4 = locParticleComboStep->Get_SpacetimeVertex();

				//beam & production vertex
				Particle_t locPID = locParticleComboStep->Get_InitialParticleID();
				if((locPID == Gamma) || (locPID == Electron) || (locPID == Positron))
				{
					const DKinematicData* locKinematicData = locParticleComboStep->Get_InitialParticle();
					const DKinematicData* locKinematicData_Measured = locParticleComboStep->Get_InitialParticle_Measured();
					if(locKinematicData_Measured != NULL) //missing beam particle
						Fill_BeamParticleData(locTree, locKinematicData, locKinematicData_Measured, locBeamToIDMap);
					TLorentzVector locX4_Production(locStepX4.X(), locStepX4.Y(), locStepX4.Z(), locStepX4.T());
					Fill_TObjectData<TLorentzVector>(locTree, "", "X4_Production", &locX4_Production, (*dTObjectMap)[locTree->GetName()]);
				}
				else if(IsDetachedVertex(locPID))
				{
					//decay vertices
					string locVariableName = string("X4_") + Convert_ToBranchName(ParticleType(locPID)) + string("Decay");
					TLorentzVector locX4_Decay(locStepX4.X(), locStepX4.Y(), locStepX4.Z(), locStepX4.T());
					Fill_TObjectData<TLorentzVector>(locTree, "", locVariableName, &locX4_Decay, (*dTObjectMap)[locTree->GetName()]);
				}

				//final state particles
				for(size_t loc_l = 0; loc_l < locParticleComboStep->Get_NumFinalParticles(); ++loc_l)
				{
					const DKinematicData* locKinematicData_Measured = locParticleComboStep->Get_FinalParticle_Measured(loc_l);
					if(locKinematicData_Measured == NULL)
						continue;

					//remove from unused lists
					const JObject* locSourceObject = locParticleComboStep->Get_FinalParticle_SourceObject(loc_l);
					if(locParticleComboStep->Is_FinalParticleCharged(loc_l))
					{
						const DChargedTrack* locChargedTrack = dynamic_cast<const DChargedTrack*>(locSourceObject);
						oid_t locTrackID = locChargedTrack->Get_BestFOM()->candidateid;
						for(size_t loc_m = 0; loc_m < locUnusedChargedTrackHypotheses.size(); ++loc_m)
						{
							if((locUnusedChargedTrackHypotheses[loc_m]->candidateid != locTrackID) || (locUnusedChargedTrackHypotheses[loc_m]->PID() != locKinematicData_Measured->PID()))
								continue;
							locUnusedChargedTrackHypotheses.erase(locUnusedChargedTrackHypotheses.begin() + loc_m);
							break;
						}
					}
					else
					{
						const DNeutralShower* locSourceNeutralShower = dynamic_cast<const DNeutralShower*>(locSourceObject);
						for(size_t loc_m = 0; loc_m < locUnusedNeutralParticleHypotheses.size(); ++loc_m)
						{
							const DNeutralShower* locAssociatedNeutralShower = NULL;
							locUnusedNeutralParticleHypotheses[loc_m]->GetSingleT(locAssociatedNeutralShower);
							if((locSourceNeutralShower != locAssociatedNeutralShower) || (locUnusedNeutralParticleHypotheses[loc_m]->PID() != locKinematicData_Measured->PID()))
								continue;
							locUnusedNeutralParticleHypotheses.erase(locUnusedNeutralParticleHypotheses.begin() + loc_m);
							break;
						}
					}

					//get the branch name
					ostringstream locPositionStream;
					locPositionStream << loc_k << "_" << loc_l;
					TObjString* locObjString = (TObjString*)locPositionToNameMap->GetValue(locPositionStream.str().c_str());
					string locParticleBranchName = (const char*)(locObjString->GetString());

					//fill the data
					const DKinematicData* locKinematicData = locParticleComboStep->Get_FinalParticle(loc_l);
					Fill_ParticleData(locTree, locParticleBranchName, locKinematicData, locKinematicData_Measured, locShowerToIDMap, locMCThrownMatching);
				}
			}

			//unused
			unsigned int locNumUnused = locUnusedChargedTrackHypotheses.size() + locUnusedNeutralParticleHypotheses.size();
			Fill_FundamentalData<UInt_t>(locTree, "NumUnused", locNumUnused);
			for(size_t loc_j = 0; loc_j < locUnusedChargedTrackHypotheses.size(); ++loc_j)
				Fill_UnusedParticleData(locTree, loc_j, locNumUnused, locUnusedChargedTrackHypotheses[loc_j], locShowerToIDMap, locMCThrownMatching);
			for(size_t loc_j = 0; loc_j < locUnusedNeutralParticleHypotheses.size(); ++loc_j)
				Fill_UnusedParticleData(locTree, loc_j + locUnusedChargedTrackHypotheses.size(), locNumUnused, locUnusedNeutralParticleHypotheses[loc_j], locShowerToIDMap, locMCThrownMatching);
			//update array sizes
			if(locNumUnused > (*dNumUnusedArraySizeMap)[locTree])
				(*dNumUnusedArraySizeMap)[locTree] = locNumUnused;

			locTree->Fill();
		}
	}
	japp->RootUnLock();
}

ULong64_t DEventWriterROOT::Calc_ParticleMultiplexID(Particle_t locPID) const
{
	int locPower = ParticleMultiplexPower(locPID);
	if(locPower == -1)
		return 0;

	int locIsFinalStateInt = Is_FinalStateParticle(locPID);
	if(locPID == Pi0)
		locIsFinalStateInt = 1;

	if(locIsFinalStateInt == 1) //decimal
	{
		ULong64_t locParticleMultiplexID = 1;
		for(int loc_i = 0; loc_i < locPower; ++loc_i)
			locParticleMultiplexID *= ULong64_t(10);
		return locParticleMultiplexID;
	}
	//decaying: binary
	return (ULong64_t(1) << ULong64_t(locPower)); //bit-shift
}

void DEventWriterROOT::Fill_ThrownParticleData(TTree* locTree, unsigned int locArrayIndex, unsigned int locMinArraySize, const DMCThrown* locMCThrown) const
{
	map<const DNeutralShower*, int> locShowerToIDMap;
	Fill_ThrownParticleData(locTree, locArrayIndex, locMinArraySize, locMCThrown, NULL, locShowerToIDMap);
}

void DEventWriterROOT::Fill_ThrownParticleData(TTree* locTree, unsigned int locArrayIndex, unsigned int locMinArraySize, const DMCThrown* locMCThrown, const DMCThrownMatching* locMCThrownMatching, const map<const DNeutralShower*, int>& locShowerToIDMap) const
{
	//IDENTIFIERS
	Fill_FundamentalData<UInt_t>(locTree, "Thrown", "ObjectID", locMCThrown->myid, locArrayIndex, locMinArraySize, (*dNumThrownArraySizeMap)[locTree]);
	Fill_FundamentalData<UInt_t>(locTree, "Thrown", "ParentID", locMCThrown->parentid, locArrayIndex, locMinArraySize, (*dNumThrownArraySizeMap)[locTree]);
	Fill_FundamentalData<Int_t>(locTree, "Thrown", "PID_PDG", locMCThrown->pdgtype, locArrayIndex, locMinArraySize, (*dNumThrownArraySizeMap)[locTree]);
	Int_t locMatchID = -1;
	if(locMCThrownMatching != NULL)
	{
		if(ParticleCharge(locMCThrown->PID()) != 0)
		{
			const DChargedTrack* locChargedTrack = locMCThrownMatching->Get_MatchingChargedTrack(locMCThrown);
			if(locChargedTrack != NULL)
				locMatchID = locChargedTrack->Get_BestFOM()->candidateid;
		}
		else
		{
			const DNeutralParticle* locNeutralParticle = locMCThrownMatching->Get_MatchingNeutralParticle(locMCThrown);
			if(locNeutralParticle != NULL)
			{
				const DNeutralShower* locNeutralShower = NULL;
				locNeutralParticle->GetSingleT(locNeutralShower);
				locMatchID = (locShowerToIDMap.find(locNeutralShower))->second;
			}
		}
		Fill_FundamentalData<Int_t>(locTree, "Thrown", "MatchID", locMatchID, locArrayIndex, locMinArraySize, (*dNumThrownArraySizeMap)[locTree]);
	}

	//KINEMATICS: THROWN //at the production vertex
	TLorentzVector locX4_Thrown(locMCThrown->position().X(), locMCThrown->position().Y(), locMCThrown->position().Z(), locMCThrown->time());
	Fill_ClonesData<TLorentzVector>(locTree, "Thrown", "X4_Thrown", &locX4_Thrown, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);
	TLorentzVector locP4_Thrown(locMCThrown->momentum().X(), locMCThrown->momentum().Y(), locMCThrown->momentum().Z(), locMCThrown->energy());
	Fill_ClonesData<TLorentzVector>(locTree, "Thrown", "P4_Thrown", &locP4_Thrown, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);
}

void DEventWriterROOT::Fill_BeamParticleData(TTree* locTree, const DKinematicData* locKinematicData, const DKinematicData* locKinematicData_Measured, const map<const DBeamPhoton*, int>& locBeamToIDMap) const
{
	//IDENTIFIER
	const DBeamPhoton* locBeamPhoton = dynamic_cast<const DBeamPhoton*>(locKinematicData_Measured);
	Fill_FundamentalData<Int_t>(locTree, "Beam", "ObjectID", locBeamToIDMap.find(locBeamPhoton)->second);

	//KINEMATICS: MEASURED
	TLorentzVector locX4_Measured(locKinematicData_Measured->position().X(), locKinematicData_Measured->position().Y(), locKinematicData_Measured->position().Z(), locKinematicData_Measured->time());
	Fill_TObjectData<TLorentzVector>(locTree, "Beam", "X4_Measured", &locX4_Measured, (*dTObjectMap)[locTree->GetName()]);
	TLorentzVector locP4_Measured(locKinematicData_Measured->momentum().X(), locKinematicData_Measured->momentum().Y(), locKinematicData_Measured->momentum().Z(), locKinematicData_Measured->energy());
	Fill_TObjectData<TLorentzVector>(locTree, "Beam", "P4_Measured", &locP4_Measured, (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: KINFIT
	if(locKinematicData != locKinematicData_Measured)
	{
		TLorentzVector locX4_KinFit(locKinematicData->position().X(), locKinematicData->position().Y(), locKinematicData->position().Z(), locKinematicData->time());
		Fill_TObjectData<TLorentzVector>(locTree, "Beam", "X4_KinFit", &locX4_KinFit, (*dTObjectMap)[locTree->GetName()]);
		TLorentzVector locP4_KinFit(locKinematicData->momentum().X(), locKinematicData->momentum().Y(), locKinematicData->momentum().Z(), locKinematicData->energy());
		Fill_TObjectData<TLorentzVector>(locTree, "Beam", "P4_KinFit", &locP4_KinFit, (*dTObjectMap)[locTree->GetName()]);
	}
}

void DEventWriterROOT::Fill_ParticleData(TTree* locTree, string locParticleBranchName, const DKinematicData* locKinematicData, const DKinematicData* locKinematicData_Measured, const map<const DNeutralShower*, int>& locShowerToIDMap, const DMCThrownMatching* locMCThrownMatching) const
{
	bool locKinFitPerformedFlag = 	(locKinematicData != locKinematicData_Measured);
	//KINEMATICS: MEASURED
	TLorentzVector locX4_Measured(locKinematicData_Measured->position().X(), locKinematicData_Measured->position().Y(), locKinematicData_Measured->position().Z(), locKinematicData_Measured->time());
	Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "X4_Measured", &locX4_Measured, (*dTObjectMap)[locTree->GetName()]);
	TLorentzVector locP4_Measured(locKinematicData_Measured->momentum().X(), locKinematicData_Measured->momentum().Y(), locKinematicData_Measured->momentum().Z(), locKinematicData_Measured->energy());
	Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "P4_Measured", &locP4_Measured, (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: END //v3 & p4 are bad (need to swim still, get hit pos from shower, etc.)
	TLorentzVector locX4_End(numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), locKinematicData->time());
	Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "X4_End", &locX4_End, (*dTObjectMap)[locTree->GetName()]);
	TLorentzVector locP4_End(numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN());
	Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "P4_End", &locP4_End, (*dTObjectMap)[locTree->GetName()]);

	//KINEMATICS: KINFIT
	if(locKinFitPerformedFlag)
	{
		TLorentzVector locX4_KinFit(locKinematicData->position().X(), locKinematicData->position().Y(), locKinematicData->position().Z(), locKinematicData->time());
		Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "X4_KinFit", &locX4_KinFit, (*dTObjectMap)[locTree->GetName()]);
		TLorentzVector locP4_KinFit(locKinematicData->momentum().X(), locKinematicData->momentum().Y(), locKinematicData->momentum().Z(), locKinematicData->energy());
		Fill_TObjectData<TLorentzVector>(locTree, locParticleBranchName, "P4_KinFit", &locP4_KinFit, (*dTObjectMap)[locTree->GetName()]);
	}

	//KINEMATICS: OTHER
	Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "PathLength", locKinematicData->pathLength());

	const DChargedTrackHypothesis* locChargedTrackHypothesis = dynamic_cast<const DChargedTrackHypothesis*>(locKinematicData);
	if(locChargedTrackHypothesis != NULL)
	{
		const DChargedTrackHypothesis* locChargedTrackHypothesis_Measured = dynamic_cast<const DChargedTrackHypothesis*>(locKinematicData_Measured);

		//associated objects
		const DTrackTimeBased* locTrackTimeBased = NULL;
		locChargedTrackHypothesis->GetSingleT(locTrackTimeBased);
		const DBCALShower* locBCALShower = NULL;
		locChargedTrackHypothesis->GetSingleT(locBCALShower);
		const DFCALShower* locFCALShower = NULL;
		locChargedTrackHypothesis->GetSingleT(locFCALShower);

		//IDENTIFIER / MATCHING
		Fill_FundamentalData<Int_t>(locTree, locParticleBranchName, "ObjectID", locChargedTrackHypothesis->candidateid);
		Int_t locMatchID = -1;
		if(locMCThrownMatching != NULL)
		{
			const DMCThrown* locMCThrown = locMCThrownMatching->Get_MatchingMCThrown(locChargedTrackHypothesis_Measured);
			if(locMCThrown != NULL)
				locMatchID = locMCThrown->myid;
		}
		Fill_FundamentalData<Int_t>(locTree, locParticleBranchName, "MatchID", locMatchID);

		//PID QUALITY
		Fill_FundamentalData<UInt_t>(locTree, locParticleBranchName, "NDF_Tracking", locChargedTrackHypothesis->dNDF_Track);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_Tracking", locChargedTrackHypothesis->dChiSq_Track);
		Fill_FundamentalData<UInt_t>(locTree, locParticleBranchName, "NDF_Timing", locChargedTrackHypothesis->dNDF_Timing);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_Measured", locChargedTrackHypothesis_Measured->dChiSq_Timing);
		if(locKinFitPerformedFlag)
			Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_KinFit", locChargedTrackHypothesis->dChiSq_Timing);
		Fill_FundamentalData<UInt_t>(locTree, locParticleBranchName, "NDF_DCdEdx", locChargedTrackHypothesis->dNDF_DCdEdx);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_DCdEdx", locChargedTrackHypothesis->dChiSq_DCdEdx);


		//DEPOSITED ENERGY
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "dEdx_CDC", locTrackTimeBased->ddEdx_CDC);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "dEdx_FDC", locTrackTimeBased->ddEdx_FDC);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "dEdx_TOF", locChargedTrackHypothesis->dTOFdEdx);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "dEdx_ST", locChargedTrackHypothesis->dStartCounterdEdx);

		double locBCALEnergy = (locBCALShower != NULL) ? locBCALShower->E : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "Energy_BCAL", locBCALEnergy);
		double locFCALEnergy = (locFCALShower != NULL) ? locFCALShower->getEnergy() : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "Energy_FCAL", locFCALEnergy);
	}
	else
	{
		const DNeutralParticleHypothesis* locNeutralParticleHypothesis = dynamic_cast<const DNeutralParticleHypothesis*>(locKinematicData);
		const DNeutralParticleHypothesis* locNeutralParticleHypothesis_Measured = dynamic_cast<const DNeutralParticleHypothesis*>(locKinematicData_Measured);

		//associated objects
		const DNeutralShower* locNeutralShower = NULL;
		locNeutralParticleHypothesis->GetSingleT(locNeutralShower);
		const DBCALShower* locBCALShower = NULL;
		locNeutralShower->GetSingleT(locBCALShower);
		const DFCALShower* locFCALShower = NULL;
		locNeutralShower->GetSingleT(locFCALShower);

		//IDENTIFIER / MATCHING
		Fill_FundamentalData<Int_t>(locTree, locParticleBranchName, "ObjectID", (locShowerToIDMap.find(locNeutralShower))->second);
		Int_t locMatchID = -1;
		if(locMCThrownMatching != NULL)
		{
			const DMCThrown* locMCThrown = locMCThrownMatching->Get_MatchingMCThrown(locNeutralParticleHypothesis_Measured);
			if(locMCThrown != NULL)
				locMatchID = locMCThrown->myid;
		}
		Fill_FundamentalData<Int_t>(locTree, locParticleBranchName, "MatchID", locMatchID);

		//PID QUALITY
		Fill_FundamentalData<UInt_t>(locTree, locParticleBranchName, "NDF_Timing", locNeutralParticleHypothesis->dNDF);
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_Measured", locNeutralParticleHypothesis_Measured->dChiSq);
		if(locKinFitPerformedFlag)
			Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "ChiSq_Timing_KinFit", locNeutralParticleHypothesis->dChiSq);

		//DEPOSITED ENERGY
		double locBCALEnergy = (locBCALShower != NULL) ? locBCALShower->E : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "Energy_BCAL", locBCALEnergy);
		double locFCALEnergy = (locFCALShower != NULL) ? locFCALShower->getEnergy() : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, locParticleBranchName, "Energy_FCAL", locFCALEnergy);
	}
}

void DEventWriterROOT::Fill_UnusedParticleData(TTree* locTree, unsigned int locArrayIndex, unsigned int locMinArraySize, const DKinematicData* locKinematicData, const map<const DNeutralShower*, int>& locShowerToIDMap, const DMCThrownMatching* locMCThrownMatching) const
{
	//KINEMATICS: MEASURED
	TLorentzVector locX4_Measured(locKinematicData->position().X(), locKinematicData->position().Y(), locKinematicData->position().Z(), locKinematicData->time());
	Fill_ClonesData<TLorentzVector>(locTree, "Unused", "X4_Measured", &locX4_Measured, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);
	TLorentzVector locP4_Measured(locKinematicData->momentum().X(), locKinematicData->momentum().Y(), locKinematicData->momentum().Z(), locKinematicData->energy());
	Fill_ClonesData<TLorentzVector>(locTree, "Unused", "P4_Measured", &locP4_Measured, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);

	//KINEMATICS: END //v3 & p4 are bad (need to swim still, get hit pos from shower, etc.)
	TLorentzVector locX4_End(numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), locKinematicData->t1());
	Fill_ClonesData<TLorentzVector>(locTree, "Unused", "X4_End", &locX4_End, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);
	TLorentzVector locP4_End(numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN());
	Fill_ClonesData<TLorentzVector>(locTree, "Unused", "P4_End", &locP4_End, locArrayIndex, (*dClonesArrayMap)[locTree->GetName()]);

	//KINEMATICS: OTHER
	Fill_FundamentalData<Double_t>(locTree, "Unused", "PathLength", locKinematicData->pathLength(), locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);

	const DChargedTrackHypothesis* locChargedTrackHypothesis = dynamic_cast<const DChargedTrackHypothesis*>(locKinematicData);
	if(locChargedTrackHypothesis != NULL)
	{
		//associated objects
		const DTrackTimeBased* locTrackTimeBased = NULL;
		locChargedTrackHypothesis->GetSingleT(locTrackTimeBased);
		const DBCALShower* locBCALShower = NULL;
		locChargedTrackHypothesis->GetSingleT(locBCALShower);
		const DFCALShower* locFCALShower = NULL;
		locChargedTrackHypothesis->GetSingleT(locFCALShower);

		//IDENTIFIERS / MATCHING
		Fill_FundamentalData<Int_t>(locTree, "Unused", "ObjectID", locChargedTrackHypothesis->candidateid, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Int_t locMatchID = -1;
		if(locMCThrownMatching != NULL)
		{
			const DMCThrown* locMCThrown = locMCThrownMatching->Get_MatchingMCThrown(locChargedTrackHypothesis);
			if(locMCThrown != NULL)
				locMatchID = locMCThrown->myid;
		}
		Fill_FundamentalData<Int_t>(locTree, "Unused", "MatchID", locMatchID, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "PID", (unsigned int)locKinematicData->PID(), locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);

		//PID QUALITY
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "NDF_Tracking", locChargedTrackHypothesis->dNDF_Track, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "ChiSq_Tracking", locChargedTrackHypothesis->dChiSq_Track, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "NDF_Timing", locChargedTrackHypothesis->dNDF_Timing, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "ChiSq_Timing", locChargedTrackHypothesis->dChiSq_Timing, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "NDF_DCdEdx", locChargedTrackHypothesis->dNDF_DCdEdx, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "ChiSq_DCdEdx", locChargedTrackHypothesis->dChiSq_DCdEdx, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);

		//DEPOSITED ENERGY
		Fill_FundamentalData<Double_t>(locTree, "Unused", "dEdx_CDC", locTrackTimeBased->ddEdx_CDC, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "dEdx_FDC", locTrackTimeBased->ddEdx_FDC, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "dEdx_TOF", locChargedTrackHypothesis->dTOFdEdx, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "dEdx_ST", locChargedTrackHypothesis->dStartCounterdEdx, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		double locBCALEnergy = (locBCALShower != NULL) ? locBCALShower->E : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, "Unused", "Energy_BCAL", locBCALEnergy, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		double locFCALEnergy = (locFCALShower != NULL) ? locFCALShower->getEnergy() : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, "Unused", "Energy_FCAL", locFCALEnergy, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
	}
	else
	{
		const DNeutralParticleHypothesis* locNeutralParticleHypothesis = dynamic_cast<const DNeutralParticleHypothesis*>(locKinematicData);

		//associated objects
		const DNeutralShower* locNeutralShower = NULL;
		locNeutralParticleHypothesis->GetSingleT(locNeutralShower);
		const DBCALShower* locBCALShower = NULL;
		locNeutralShower->GetSingleT(locBCALShower);
		const DFCALShower* locFCALShower = NULL;
		locNeutralShower->GetSingleT(locFCALShower);

		//IDENTIFIERS
		Fill_FundamentalData<Int_t>(locTree, "Unused", "ObjectID", (locShowerToIDMap.find(locNeutralShower))->second, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Int_t locMatchID = -1;
		if(locMCThrownMatching != NULL)
		{
			const DMCThrown* locMCThrown = locMCThrownMatching->Get_MatchingMCThrown(locNeutralParticleHypothesis);
			if(locMCThrown != NULL)
				locMatchID = locMCThrown->myid;
		}
		Fill_FundamentalData<Int_t>(locTree, "Unused", "MatchID", locMatchID, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "PID", (unsigned int)locKinematicData->PID(), locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);

		//PID QUALITY
		Fill_FundamentalData<UInt_t>(locTree, "Unused", "NDF_Timing", locNeutralParticleHypothesis->dNDF, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		Fill_FundamentalData<Double_t>(locTree, "Unused", "ChiSq_Timing", locNeutralParticleHypothesis->dChiSq, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);

		//DEPOSITED ENERGY
		double locBCALEnergy = (locBCALShower != NULL) ? locBCALShower->E : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, "Unused", "Energy_BCAL", locBCALEnergy, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
		double locFCALEnergy = (locFCALShower != NULL) ? locFCALShower->getEnergy() : numeric_limits<double>::quiet_NaN();
		Fill_FundamentalData<Double_t>(locTree, "Unused", "Energy_FCAL", locFCALEnergy, locArrayIndex, locMinArraySize, (*dNumUnusedArraySizeMap)[locTree]);
	}
}
