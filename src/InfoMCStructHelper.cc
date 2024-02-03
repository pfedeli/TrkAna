//
// Namespace for collecting tools used in MC truth evaluation
// Original author: Dave Brown (LBNL) 8/10/2016
//
#include "TrkAna/inc/InfoMCStructHelper.hh"
#include "Offline/MCDataProducts/inc/StepPointMC.hh"
#include "Offline/MCDataProducts/inc/SimParticle.hh"
#include "Offline/MCDataProducts/inc/MCRelationship.hh"

#include "Offline/TrackerGeom/inc/Tracker.hh"
#include "Offline/Mu2eUtilities/inc/TwoLinePCA.hh"
#include "Offline/DataProducts/inc/EventWindowMarker.hh"
#include "Offline/DataProducts/inc/VirtualDetectorId.hh"

#include "Offline/GlobalConstantsService/inc/GlobalConstantsHandle.hh"
#include "Offline/GlobalConstantsService/inc/ParticleDataList.hh"
#include "Offline/GlobalConstantsService/inc/PhysicsParams.hh"
#include "Offline/GeometryService/inc/GeomHandle.hh"
#include "Offline/GeometryService/inc/DetectorSystem.hh"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Event.h"

#include <map>
#include <limits>

namespace mu2e {

  InfoMCStructHelper::InfoMCStructHelper(const Config& conf) :
    _spctag(conf.spctag()), _mingood(conf.mingood()),
    _maxdt(conf.maxvddt()),
    _ewMarkerTag(conf.ewMarkerTag()) {
      // build the VDId -> SId map by hand
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_FrontHollow)] = SurfaceId("TT_Front");
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_Mid)] = SurfaceId("TT_Mid");
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_MidInner)] = SurfaceId("TT_Mid");
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_Back)] = SurfaceId("TT_Back");
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_OutSurf)] = SurfaceId("TT_Outer");
      _vdmap[VirtualDetectorId(VirtualDetectorId::TT_InSurf)] = SurfaceId("TT_Inner");
    }

  void InfoMCStructHelper::updateEvent(const art::Event& event) {
    event.getByLabel(_spctag,_spcH);
    _mbtime = GlobalConstantsHandle<PhysicsParams>()->getNominalDRPeriod();
    art::Handle<EventWindowMarker> ewMarkerHandle;
    event.getByLabel(_ewMarkerTag, ewMarkerHandle);
    const EventWindowMarker& ewMarker(*ewMarkerHandle);
    _onSpill = (ewMarker.spillType() == EventWindowMarker::SpillType::onspill);
  }

  void InfoMCStructHelper::fillTrkInfoMC(const KalSeed& kseed, const KalSeedMC& kseedmc, TrkInfoMC& trkinfomc) {
    // use the primary match of the track
    // primary associated SimParticle
    if(kseedmc.simParticles().size() > 0){
      auto const& simp = kseedmc.simParticles().front();
      trkinfomc.valid = true;
      trkinfomc.nhits = simp._nhits; // number of hits from the primary particle
      trkinfomc.nactive = simp._nactive; // number of active hits from the primary particle
    }

    fillTrkInfoMCDigis(kseed, kseedmc, trkinfomc);
  }

  void InfoMCStructHelper::fillTrkInfoMCDigis(const KalSeed& kseed, const KalSeedMC& kseedmc, TrkInfoMC& trkinfomc) {
    trkinfomc.ndigi = 0; trkinfomc.ndigigood = 0, trkinfomc.nambig = 0;
    // find the first segment momentum as reference
    double simmom = 1.0;
    if(kseedmc.simParticles().size()>0)
      simmom = sqrt(kseedmc.simParticles().front()._mom.mag2());
    for(size_t i_digi = 0; i_digi < kseedmc._tshmcs.size(); ++i_digi) {
      const auto& tshmc = kseedmc._tshmcs.at(i_digi);

      if (kseedmc.simParticle(tshmc._spindex)._rel == MCRelationship::same) {
        ++trkinfomc.ndigi;
        if(sqrt(tshmc.particleMomentum().mag2())/simmom > _mingood) {
          ++trkinfomc.ndigigood;
        }

        // easiest way to get MC ambiguity is through info object
        TrkStrawHitInfoMC tshinfomc;
        fillHitInfoMC(kseedmc,tshinfomc,tshmc);
        // the MCDigi list can be longer than the # of TrkStrawHits in the seed:
        if(i_digi < kseed.hits().size()){
          const auto& ihit = kseed.hits().at(i_digi);
          if(ihit.flag().hasAllProperties(StrawHitFlag::active) && ihit.ambig()*tshinfomc.ambig > 0) {
            ++trkinfomc.nambig;
          }
        }
      }
    }
  }

  void InfoMCStructHelper::fillHitInfoMC(const KalSeedMC& kseedmc, TrkStrawHitInfoMC& tshinfomc, const TrkStrawHitMC& tshmc) {
    const Tracker& tracker = *GeomHandle<Tracker>();

    const SimPartStub& simPart = kseedmc.simParticle(tshmc._spindex);
    tshinfomc.pdg = simPart._pdg;
    tshinfomc.proc = simPart._proc;
    tshinfomc.gen = simPart._gid.id();
    tshinfomc.rel = simPart._rel;
    tshinfomc.earlyend = tshmc._earlyend._end;
    tshinfomc.t0 = tshmc._time;
    tshinfomc.tdrift = tshmc._tdrift;
    tshinfomc.rdrift = tshmc._rdrift;
    tshinfomc.tprop = tshmc._tprop;
    tshinfomc.edep = tshmc._energySum;
    tshinfomc.mom = std::sqrt(tshmc._mom.mag2());
    tshinfomc.cpos  = tshmc._cpos;

    // find the step midpoint
    const Straw& straw = tracker.getStraw(tshmc._strawId);
    auto mcsep = tshmc._cpos-XYZVectorF(straw.getMidPoint());
    auto wdir = XYZVectorF(straw.getDirection());
    tshinfomc.len = mcsep.Dot(wdir);
    auto mdir = tshmc._mom.Unit();
    auto mcperp = mdir.Cross(wdir).Unit();
    double dperp = mcperp.Dot(mcsep);
    tshinfomc.twdot = mdir.Dot(wdir);
    tshinfomc.dist = fabs(dperp);
    auto wperp = wdir.Cross(mcperp);
    tshinfomc.tau = mcsep.Dot(wperp);
    tshinfomc.cdist = sqrt(tshinfomc.dist*tshinfomc.dist+tshinfomc.tau*tshinfomc.tau);
    tshinfomc.ambig = dperp > 0 ? -1 : 1; // follow TrkPoca convention
    tshinfomc.doca = -1*dperp;
  }

  void InfoMCStructHelper::fillAllSimInfos(const KalSeedMC& kseedmc, const PrimaryParticle& primary, std::vector<SimInfo>& siminfos, int n_generations, int n_match) {
    // interpret -1 as no llimit
    if (n_generations == -1) {
      n_generations = std::numeric_limits<int>::max();
    }
    if (n_match == -1 ) {
      n_match = std::numeric_limits<int>::max();
    }
    for(int imatch = 0 ; imatch < std::min(n_match,static_cast<int>(kseedmc.simParticles().size())); ++imatch) {
      auto trkprimaryptr = kseedmc.simParticle(imatch).simParticle(_spcH);
      auto trkprimary = trkprimaryptr->originParticle();
      auto current_sim_particle_ptr = trkprimaryptr;
      auto current_sim_particle = trkprimary;

      for (int i_generation = 0; i_generation < n_generations; ++i_generation) {
        SimInfo sim_info;
        fillSimInfo(current_sim_particle, sim_info);
        sim_info.trkrel = MCRelationship(current_sim_particle_ptr, trkprimaryptr);
        sim_info.rank = imatch;

        auto bestprimarysp = primary.primarySimParticles().front();
        MCRelationship bestrel;
        for(auto const& spp : primary.primarySimParticles()){
          MCRelationship mcrel(current_sim_particle_ptr, spp);
          if(mcrel > bestrel){
            bestrel = mcrel;
            bestprimarysp = spp;
          }
        }
        sim_info.prirel = bestrel;
        // only count hits for direct contributors
        if(i_generation == 0){
          sim_info.nhits = kseedmc.simParticle(imatch)._nhits;
          sim_info.nactive = kseedmc.simParticle(imatch)._nactive;
        }
        // record the index this object will have
        sim_info.index = siminfos.size();
        siminfos.push_back(sim_info);
        if (current_sim_particle.parent().isNonnull()) {
          current_sim_particle_ptr = current_sim_particle.parent();
          current_sim_particle = current_sim_particle_ptr->originParticle();
        }
        else {
          break; // this particle doesn't have a parent
        }
      }
    }

    // Now add all the primary particles
    SimInfo sim_info;
    for(auto const& spp : primary.primarySimParticles()){
      fillSimInfo(spp, sim_info);

      // check whether we already put this primary in
      bool already_added = false;
      for (const auto& i_sim_info : siminfos) {
        if (i_sim_info.prirel == MCRelationship::same) {
          already_added = true;
          break;
        }
      }
      if (!already_added) {
        auto trkprimaryptr = kseedmc.simParticle().simParticle(_spcH);
        sim_info.trkrel = MCRelationship(spp, trkprimaryptr);
        sim_info.prirel = MCRelationship(spp, spp);
        sim_info.index = siminfos.size();
        siminfos.push_back(sim_info);
      }
    }
  }


  void InfoMCStructHelper::fillSimInfo(const art::Ptr<SimParticle>& sp, SimInfo& siminfo) {
    if(sp.isNonnull()){
      fillSimInfo(*sp, siminfo);
    }
    else {
      siminfo.valid = false;
    }
  }

  void InfoMCStructHelper::fillSimInfo(const SimParticle& sp, SimInfo& siminfo) {
    GeomHandle<DetectorSystem> det;
    siminfo.valid = true;
    if(sp.genParticle().isNonnull())siminfo.gen = sp.genParticle()->generatorId().id();
    siminfo.proc = sp.creationCode();
    siminfo.pdg = sp.pdgId();
    siminfo.time = sp.startGlobalTime();
    siminfo.mom = XYZVectorF(sp.startMomentum());
    siminfo.pos = XYZVectorF(det->toDetector(sp.startPosition()));
    siminfo.endmom = XYZVectorF(sp.endMomentum());
    siminfo.endpos = XYZVectorF(det->toDetector(sp.endPosition()));
    
    //rmax calculation here:
    //  make a LoopHelix by call to constructor
    //  make momentum and position vectors
    ROOT::Math::XYZTVector pos0(siminfo.pos.x(), siminfo.pos.y(), siminfo.pos.z(), sp.startGlobalTime());
    ROOT::Math::PxPyPzMVector mom0(siminfo.mom.x(), siminfo.mom.y(), siminfo.mom.z(), sp.startMomentum().t());

    // extract charge
    int charge = -1*siminfo.pdg/fabs(siminfo.pdg); //TODO - this doesnt work for some particles!!
    // extact field
    GeomHandle<BFieldManager> bfmgr;
    XYZVectorF pos_in_Mu2e = XYZVectorF(sp.startPosition());
    ROOT::Math::XYZVector bnom(bfmgr->getBField(pos_in_Mu2e).x(),bfmgr->getBField(pos_in_Mu2e).y(),bfmgr->getBField(pos_in_Mu2e).z());
    
    // make the loophelix
    KinKal::LoopHelix lh(pos0, mom0, charge, bnom);
    // calculate rmax and add maxr to siminfo
    siminfo.maxr =sqrt(lh.cx()*lh.cx()+lh.cy()*lh.cy())+fabs(lh.rad());
    // add track parameters
    siminfo.rad = lh.rad();
    siminfo.lam = lh.lam();
    siminfo.cx = lh.cx();
    siminfo.cy = lh.cy();
    siminfo.phi0= lh.phi0();
    siminfo.t0 = lh.t0();
  }

  void InfoMCStructHelper::fillVDInfo(const KalSeed& kseed, const KalSeedMC& kseedmc, std::vector<MCStepInfo>& vdinfos) {
    vdinfos.clear();
    const auto& vdsteps = kseedmc._vdsteps;
    const auto& inters = kseed.intersections();
    double tmin = std::numeric_limits<float>::max();
    double tmax = std::numeric_limits<float>::lowest();
    size_t imin(0), imax(0);

    for (const auto& vdstep : vdsteps) {
      // record VD steps close in time and space with candidate samples (intersections)
      double corrected_time;
      if(_onSpill) {
        corrected_time = std::fmod(vdstep._time,_mbtime);
      } else {
        corrected_time = vdstep._time;
      }
      double dtmin = std::numeric_limits<float>::max();
      MCStepInfo vdinfo;
      for(size_t iinter=0; iinter < inters.size(); ++iinter){
        auto const& inter = inters[iinter];
        // make sure this is the same surface and that the particles are going in the same direction
        if(_vdmap[vdstep._vdid] == inter.surfaceId() && vdstep._mom.Dot(inter.intersection().pdir_) > 0.0){
          // there could still be multiple intersections if the particle reflected: check
          double dt = fabs(inter.time() - corrected_time);
          if(dt < _maxdt && dt < dtmin) {
            dtmin = dt;
            vdinfo.time = corrected_time; // use corrected time for early, late flagging.  Not sure if this affects anything
            vdinfo.mom = XYZVectorF(vdstep._mom);
            vdinfo.pos = XYZVectorF(vdstep._pos);
            vdinfo.vid = vdstep._vdid.id();
            vdinfo.iinter = iinter;
            vdinfo.sid = inter.surfaceId().id();
          }
        }
      }
      if(vdinfo.iinter >=0){
        vdinfos.push_back(vdinfo);
        if(vdinfo.time > tmax){
          tmax = vdinfo.time;
          imax = vdinfos.size()-1;
        }
        if(vdinfo.time < tmin){
          tmin = vdinfo.time;
          imin = vdinfos.size()-1;
        }
      }
    }
    if(vdinfos.size() > 0){
      vdinfos[imin].early = true;
      vdinfos[imax].late = true;
    }
  }

  void InfoMCStructHelper::fillHitInfoMCs(const KalSeedMC& kseedmc, std::vector<TrkStrawHitInfoMC>& tshinfomcs) {
    tshinfomcs.clear();

    for(const auto& i_tshmc : kseedmc._tshmcs) {
      TrkStrawHitInfoMC tshinfomc;
      fillHitInfoMC(kseedmc, tshinfomc, i_tshmc);
      tshinfomcs.push_back(tshinfomc);
    }
  }

  void InfoMCStructHelper::fillCaloClusterInfoMC(CaloClusterMC const& ccmc, CaloClusterInfoMC& ccimc) {
    auto const& edeps = ccmc.energyDeposits();
    ccimc.nsim = edeps.size();
    ccimc.etot = ccmc.totalEnergyDep();
    if(ccmc.energyDeposits().size() > 0){
      auto const& primary = edeps.front();
      ccimc.eprimary = primary.energyDep();
      ccimc.tavg = primary.time(); // this is unnecessary FIXMI
      ccimc.tprimary = primary.time();
      ccimc.prel = primary.rel();
    }
  }

  void InfoMCStructHelper::fillExtraMCStepInfos(KalSeedMC const& kseedmc, StepPointMCCollection const& mcsteps,
      MCStepInfos& mcsic, MCStepSummaryInfo& mcssi) {
    GeomHandle<DetectorSystem> det;
    mcssi.reset();
    mcsic.clear();
    MCStepInfo mcsi;
    // only count the extra steps associated with the primary MC truth match
    auto simp = kseedmc.simParticle().simParticle(_spcH);
    //    std::cout << "KalSeedMC simp kep " << simp.key()
    //      << " start mom " << simp->startMomentum().vect()
    //      << " pos " << simp->startPosition() << std::endl;
    for(auto const& mcstep : mcsteps) {
      //     std::cout << "MCStep simp key " << mcstep.simParticle().key()
      //       << " start mom " << mcstep.simParticle()->startMomentum().vect()
      //       << " pos " << mcstep.simParticle()->startPosition() << std::endl;
      if(mcstep.simParticle().key() == simp.key()){
        // combine steps if they are in the same material.  They must be close in time too, since the particle may
        // re-enter the same material
        auto spos = XYZVectorF(det->toDetector(mcstep.position()));
        if(static_cast<int>(mcstep.volumeId()) == mcsi.vid && fabs(mcstep.time()-mcsi.time)<0.1 ){
          mcsi.de += mcstep.totalEDep();
          mcsi.dp += mcstep.postMomentum().mag() - mcstep.momentum().mag();
        } else {
          if(mcsi.valid()){
            // save this step
            mcsic.push_back(mcsi);
            // add its info to the summary
            mcssi.addStep(mcsi);
          }
          // initialize the new step
          mcsi.reset();
          mcsi.vid = mcstep.volumeId();
          mcsi.time = mcstep.time();
          mcsi.de = mcstep.totalEDep();
          mcsi.dp = mcstep.postMomentum().mag()- mcstep.momentum().mag();
          mcsi.mom = mcstep.momentum();
          mcsi.pos = spos;
        }
      }
    }
    // finalize the last step
    if(mcsi.valid()){
      mcsic.push_back(mcsi);
      mcssi.addStep(mcsi);
    }
  }
}
