// -*- C++ -*-
//
// Package:    Hcal/HcalAnalyzer
// Class:      HcalAnalyzer
// 
/**\class HcalAnalyzer HcalAnalyzer.cc Hcal/HcalAnalyzer/plugins/HcalAnalyzer.cc

 Description: [one line class summary]

 Implementation:
     [Notes on implementation]
*/
//
// Original Author:  Katharina Bierwagen
//         Created:  Thu, 05 Feb 2015 11:36:53 GMT
//
//


// system include files
#include <memory>
#include <string>
#include <map>
#include <iostream>
using namespace std;

//---------------------------------------------------------------------------   
#include "TTree.h"
#include "TFile.h"

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "RecoLocalCalo/HcalRecAlgos/interface/HcalSimpleRecAlgo.h"
#include "CalibCalorimetry/HcalAlgos/interface/HcalPulseContainmentManager.h"
#include "CondFormats/HcalObjects/interface/HcalRecoParams.h"
#include "CondFormats/HcalObjects/interface/HcalRecoParam.h"
#include "CalibCalorimetry/HcalAlgos/interface/HcalDbASCIIIO.h"

#include "DataFormats/HcalDetId/interface/HcalDetId.h"
#include "DataFormats/HcalRecHit/interface/HcalRecHitCollections.h"
#include "DataFormats/HcalRecHit/interface/HBHERecHit.h"
#include "DataFormats/HcalDigi/interface/HcalDigiCollections.h"

#include "CalibFormats/HcalObjects/interface/HcalCoderDb.h"
#include "CalibFormats/HcalObjects/interface/HcalCalibrations.h"
#include "CalibFormats/HcalObjects/interface/HcalDbService.h"
#include "CalibFormats/HcalObjects/interface/HcalDbRecord.h"
#include "CalibCalorimetry/HcalAlgos/interface/HcalPulseShapes.h"

#include "Geometry/CaloTopology/interface/HcalTopology.h"
#include "Geometry/CaloGeometry/interface/CaloGeometry.h"
#include "Geometry/Records/interface/CaloGeometryRecord.h"

//--------------------------------------------------------------------------- 
class HcalNtuplelizer;
//--------------------------------------------------------------------------- 

class HcalAnalyzer : public edm::EDAnalyzer {
public:
  explicit HcalAnalyzer(const edm::ParameterSet&);
  ~HcalAnalyzer();
  
  
private:
  virtual void beginJob();
  virtual void analyze(const edm::Event&, const edm::EventSetup&);
  virtual void endJob();
  
  //virtual void beginRun(edm::Run const&, edm::EventSetup const&);
  //virtual void endRun(edm::Run const&, edm::EventSetup const&);
  
  
private:
  bool FillHBHE;                  // Whether to store HBHE digi-level information or not                                                                      
  double TotalChargeThreshold;    // To avoid trees from overweight, only store digis above some threshold                                                    
  string sHBHERecHitCollection;   // Name of the HBHE rechit collection        
  edm::Service<TFileService> FileService;

  // Basic event coordinates                                                   
  long long RunNumber;
  long long EventNumber;
  long long LumiSection;
  long long Bunch;
  long long Orbit;
  long long Time;
  
  // HBHE rechits and digis
  int PulseCount;
  double Charge[5184][10];
  double Pedestal[5184][10];
  int IEta[5184];
  int IPhi[5184];
  int Depth[5184];
  
private:
  TTree *OutputTree;
  
  const CaloGeometry *Geometry;
  
  void ClearVariables();
  
};


//
// constructors and destructor
//
HcalAnalyzer::HcalAnalyzer(const edm::ParameterSet& iConfig)
  
{
  FillHBHE = iConfig.getUntrackedParameter<bool>("FillHBHE", true);
  TotalChargeThreshold = iConfig.getUntrackedParameter<double>("TotalChargeThreshold", 0);
  
  sHBHERecHitCollection = iConfig.getUntrackedParameter<string>("HBHERecHits","hbhereco");
}


HcalAnalyzer::~HcalAnalyzer()
{
}

// ------------ method called for each event  ------------
void
HcalAnalyzer::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup)
{
   using namespace edm;

   ClearVariables();

   // get stuff                                                                                                                                                                                             
   Handle<HBHERecHitCollection> hRecHits;
   iEvent.getByLabel(InputTag(sHBHERecHitCollection), hRecHits);

   Handle<HBHEDigiCollection> hHBHEDigis;
   iEvent.getByLabel(InputTag("hcalDigis"), hHBHEDigis);

   ESHandle<HcalDbService> hConditions;
   iSetup.get<HcalDbRecord>().get(hConditions);

   ESHandle<CaloGeometry> hGeometry;
   iSetup.get<CaloGeometryRecord>().get(hGeometry);
   Geometry = hGeometry.product();

   // basic event coordinates                                                                                                                                                                               
   RunNumber = iEvent.id().run();
   EventNumber = iEvent.id().event();
   LumiSection = iEvent.luminosityBlock();
   Bunch = iEvent.bunchCrossing();
   Orbit = iEvent.orbitNumber();
   Time = iEvent.time().value();

   // HBHE rechit maps - we want to link rechits and digis together                                                                                                                                         
   map<HcalDetId, int> RecHitIndex;
   for(int i = 0; i < (int)hRecHits->size(); i++)
     {
       HcalDetId id = (*hRecHits)[i].id();
       RecHitIndex.insert(pair<HcalDetId, int>(id, i));
     }

   // loop over digis                                                                                                                                                                                       
   for(HBHEDigiCollection::const_iterator iter = hHBHEDigis->begin(); iter != hHBHEDigis->end(); iter++)
     {
       HcalDetId id = iter->id();



       // First let's convert ADC to deposited charge                                                                                                                                                        
       const HcalCalibrations &Calibrations = hConditions->getHcalCalibrations(id);
       const HcalQIECoder *ChannelCoder = hConditions->getHcalCoder(id);
       const HcalQIEShape *Shape = hConditions->getHcalShape(ChannelCoder);
       HcalCoderDb Coder(*ChannelCoder, *Shape);
       CaloSamples Tool;
       Coder.adc2fC(*iter, Tool);

       // Total charge of the digi                                                                                                                                                                           
       double TotalCharge = 0;
       for(int i = 0; i < (int)iter->size(); i++)
         TotalCharge = TotalCharge + Tool[i] - Calibrations.pedestal(iter->sample(i).capid());

       // If total charge is smaller than threshold, don't store this rechit/digi into the tree                                                                                                              
       if(TotalCharge < TotalChargeThreshold)
	 continue;

       // Safety check - there are only 5184 channels in HBHE, but just in case...                                                                                                                           
       if(PulseCount >= 5184)
	 {
	   PulseCount = PulseCount + 1;
	   continue;
	 }

       // Fill things into the tree                                                                                                                                                                          
       for(int i = 0; i < (int)iter->size(); i++)
	 {
	   const HcalQIESample &QIE = iter->sample(i);

	   Pedestal[PulseCount][i] = Calibrations.pedestal(QIE.capid());
	   Charge[PulseCount][i] = Tool[i] - Pedestal[PulseCount][i];
	 }

       IEta[PulseCount] = id.ieta();
       IPhi[PulseCount] = id.iphi();
       Depth[PulseCount] = id.depth();

       PulseCount = PulseCount + 1;
     }


   // finally actually fill the tree                                                                                                                                                                        
   OutputTree->Fill();

}


// ------------ method called once each job just before starting event loop  ------------
void 
HcalAnalyzer::beginJob()
{
  // Make branches in the output trees                                                                                                                                                                     
  OutputTree = FileService->make<TTree>("HcalTree", "Hcal tree");

  OutputTree->Branch("RunNumber", &RunNumber, "RunNumber/LL");
  OutputTree->Branch("EventNumber", &EventNumber, "EventNumber/LL");
  OutputTree->Branch("LumiSection", &LumiSection, "LumiSection/LL");
  OutputTree->Branch("Bunch", &Bunch, "Bunch/LL");
  OutputTree->Branch("Orbit", &Orbit, "Orbit/LL");
  OutputTree->Branch("Time", &Time, "Time/LL");



  if(FillHBHE == true)
    {
      OutputTree->Branch("PulseCount", &PulseCount, "PulseCount/I");
      OutputTree->Branch("Charge", &Charge, "Charge[5184][10]/D");
      OutputTree->Branch("Pedestal", &Pedestal, "Pedestal[5184][10]/D");
      OutputTree->Branch("IEta", &IEta, "IEta[5184]/I");
      OutputTree->Branch("IPhi", &IPhi, "IPhi[5184]/I");
      OutputTree->Branch("Depth", &Depth, "Depth[5184]/I");
    }

}

// ------------ method called once each job just after ending the event loop  ------------
void 
HcalAnalyzer::endJob() 
{
}

// ------------ method called when starting to processes a run  ------------
/*
void 
HcalAnalyzer::beginRun(edm::Run const&, edm::EventSetup const&)
{
}
*/

// ------------ method called when ending the processing of a run  ------------
/*
void 
HcalAnalyzer::endRun(edm::Run const&, edm::EventSetup const&)
{
}
*/

 //------------------------------------------------------------------------------                                                                                                                            
void HcalAnalyzer::ClearVariables()
{
  RunNumber = 0;
  EventNumber = 0;
  LumiSection = 0;
  Bunch = 0;
  Orbit = 0;
  Time = 0;

  PulseCount = 0;
  for(int i = 0; i < 5184; i++)
    {
      for(int j = 0; j < 10; j++)
	{
	  Charge[i][j] = 0;
	  Pedestal[i][j] = 0;
	}


      IEta[i] = 0;
      IPhi[i] = 0;
      Depth[i] = 0;

    }

}

//define this as a plug-in
DEFINE_FWK_MODULE(HcalAnalyzer);