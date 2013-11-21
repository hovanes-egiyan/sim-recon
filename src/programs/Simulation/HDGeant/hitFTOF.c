/*
 * hitFTOF - registers hits for forward Time-Of-Flight
 *
 *	This is a part of the hits package for the
 *	HDGeant simulation program for Hall D.
 *
 *	version 1.0 	-Richard Jones July 16, 2001
 *
 * changes:     -B. Zihlmann June 19. 2007
 *          add hit position to north and south hit structure
 *          set THRESH_MEV to zero to NOT concatenate hits.
 *
 * changes: Wed Jun 20 13:19:56 EDT 2007 B. Zihlmann 
 *          add ipart to the function hitforwardTOF
 *
 * Programmer's Notes:
 * -------------------
 * 1) In applying the attenuation to light propagating down to both ends
 *    of the counters, there has to be some point where the attenuation
 *    factor is 1.  I chose it to be the midplane, so that in the middle
 *    of the counter both ends see the unattenuated dE values.  Closer to
 *    either end, that end has a larger dE value and the opposite end a
 *    lower dE value than the actual deposition.
 * 2) In applying the propagation delay to light propagating down to the
 *    ends of the counters, there has to be some point where the timing
 *    offset is 0.  I chose it to be the midplane, so that for hits in
 *    the middle of the counter the t values measure time-of-flight from
 *    the t=0 of the event.  For hits closer to one end, that end sees
 *    a t value smaller than its true time-of-flight, and the other end
 *    sees a value correspondingly larger.  The average is the true tof.
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <HDDM/hddm_s.h>
#include <geant3.h>
#include <bintree.h>

#include "calibDB.h"

// plastic scintillator specific constants
static float ATTEN_LENGTH =	150  ;
static float C_EFFECTIVE  =	15.0 ;
static float BAR_LENGTH   =   252.0; // length of the bar

// kinematic constants
static float TWO_HIT_RESOL =  25. ;// separation time between two different hits

static float THRESH_MEV    =  0. ;  // do not through away any hits, one can do that later

// maximum particle tracks per counter
static int MAX_HITS    =    25;   // was 100 changed to 25

// maximum MC hits per paddle
static int MAX_PAD_HITS   = 25   ;

// top level pointer of FTOF hit tree
binTree_t* forwardTOFTree = 0;

static int counterCount = 0;
static int pointCount = 0;
static int initialized = 0;


/* register hits during tracking (from gustep) */
// track is   ITRA  from GEANT
// stack is   ISTAK from GEANT
// history is ISTORY from GEANT User flag for current track history (reset to 0 in GLTRAC)

void hitForwardTOF (float xin[4], float xout[4],
                    float pin[5], float pout[5], float dEsum,
                    int track, int stack, int history, int ipart) {
  float x[3], t;
  float dx[3], dr;
  float dEdx;
  float xlocal[3];
  float xftof[3];
  float zeroHat[] = {0,0,0};

  if (!initialized) {


    mystr_t strings[50];
    float values[50];
    int nvalues = 50;
    int status = GetConstants("TOF/tof_parms", &nvalues, values, strings);

    if (!status) {

      int ncounter = 0;
      int i;
      for ( i=0;i<(int)nvalues;i++){
	//printf("%d %s \n",i,strings[i].str);
	if (!strcmp(strings[i].str,"TOF_ATTEN_LENGTH")) {
	  ATTEN_LENGTH  = values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_C_EFFECTIVE")) {
	  C_EFFECTIVE   = values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_PADDLE_LENGTH")) {
	  BAR_LENGTH    = values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_TWO_HIT_RESOL")) {
	  TWO_HIT_RESOL = values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_THRESH_MEV")) {
	  THRESH_MEV    =  values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_MAX_HITS")){
	  MAX_HITS      = (int)values[i];
	  ncounter++;
	}
	if (!strcmp(strings[i].str,"TOF_MAX_PAD_HITS")) {
	  MAX_PAD_HITS  = (int)values[i];
	  ncounter++;
	}
      }
      if (ncounter==7){
	printf("TOF: ALL parameters loaded from Data Base\n");
      } else if (ncounter<7){
	printf("TOF: NOT ALL necessary parameters found in Data Base %d out of 7\n",ncounter);
      } else {
	printf("TOF: SOME parameters found more than once in Data Base\n");
      }

    }
    initialized = 1;

  }



  // getplane is coded in
  // src/programs/Simulation/HDGeant/hddsGeant3.F
  // this file is automatically generated from the geometry file
  // written in xml format
  // NOTE: there are three files hddsGeant3.F with the same name in
  //       the source code tree namely
  //       1) src/programs/Utilities/geantbfield2root/hddsGeant3.F
  //       2) src/programs/Simulation/HDGeant/hddsGeant3.F
  //       3) src/programs/Simulation/hdds/hddsGeant3.F
  //
  //          while 2) and 3) are identical 1) is a part of 2) and 3)
  int plane = getplane_wrapper_();
  
  // calculate mean location of track and mean time in [ns] units
  // the units of xin xout and x are in [cm]
  x[0] = (xin[0] + xout[0])/2;
  x[1] = (xin[1] + xout[1])/2;
  x[2] = (xin[2] + xout[2])/2;
  t    = (xin[3] + xout[3])/2 * 1e9;
  
  // tranform the the global x coordinate into the local coordinate of the top_volume FTOF
  // defined in the geometry file src/programs/Simulation/hdds/ForwardTOF_HDDS.xml
  // the function transform Coord is defined in src/programs/Simulation/HDGeant/hitutil/hitutil.F
  transformCoord(x,"global",xlocal,"FTOF");
  transformCoord(zeroHat,"local",xftof,"FTOF");
  
  // track vector of this step
  dx[0] = xin[0] - xout[0];
  dx[1] = xin[1] - xout[1];
  dx[2] = xin[2] - xout[2];
  // length of the track of this step
  dr = sqrt(dx[0]*dx[0] + dx[1]*dx[1] + dx[2]*dx[2]);
  // calculate dEdx only if track length is >0.001 cm
  if (dr > 1e-3) {
    dEdx = dEsum/dr;
  }
  else {
    dEdx = 0;
  }

  /* post the hit to the truth tree */
  // in other words: store the GENERATED track information
  
  if ((history == 0) && (plane == 0)) { 
    
    // save all tracks from particles that hit the first plane of FTOF
    // save the generated "true" values
    
    int mark = (1<<30) + pointCount;
    
    // getTwig is defined in src/programs/Simulation/HDGeant/bintree.c
    // the two arguments are a pointer to a pointer and an integer
    
    void** twig = getTwig(&forwardTOFTree, mark);
    if (*twig == 0) {
      // make_s_ForwardTOF is defined in src/programs/Analysis/hddm/hddm_s.h
      // and coded in src/programs/Analysis/hddm/hddm_s.c
      // the same holds for make_s_FtofTruthPoints
      
      // make_s_ForwardTOF returns pointer to structure s_ForwardTOF generated memory 
      // tof->ftofCoutners and tof-> ftofTruthPoints are initialized already
      
      s_ForwardTOF_t* tof = *twig = make_s_ForwardTOF();
      s_FtofTruthPoints_t* points = make_s_FtofTruthPoints(1);
      tof->ftofTruthPoints = points;
      points->in[0].primary = (stack == 0);
      points->in[0].track = track;
      points->in[0].x = x[0];
      points->in[0].y = x[1];
      points->in[0].z = x[2];
      points->in[0].t = t;
      points->in[0].px = pin[0]*pin[4];
      points->in[0].py = pin[1]*pin[4];
      points->in[0].pz = pin[2]*pin[4];
      points->in[0].E = pin[3];
      points->in[0].ptype = ipart;
      points->mult = 1;
      pointCount++;
    }
  }
  
  /* post the hit to the hits tree, mark slab as hit */
  // in other words now store the simulated detector response
  if (dEsum > 0) {
    int nhit;
    s_FtofNorthTruthHits_t* northHits;
    s_FtofSouthTruthHits_t* southHits;

    s_FtofMCHits_t* noMCHits;
    s_FtofMCHits_t* soMCHits;

    // getrow and getcolumn are both coded in hddsGeant3.F
    // see above for function getplane()
    
    int row = getrow_wrapper_();
    int column = getcolumn_wrapper_();
    
    // distance of hit from PMT north w.r.t. center and similar for PMT south
    // this means positive x points north. to get a right handed system y must
    // point vertically up as z is the beam axis.
    // plane==0 horizontal plane, plane==1 vertical plane
    //    float dist = xlocal[0];

    float dist = x[1]; // do not use local coordinate for x and y
    if (plane==1) dist = x[0];
    float dxnorth = BAR_LENGTH/2.-dist;
    float dxsouth = BAR_LENGTH/2.+dist;
    
    // calculate time at the PMT "normalized" to the center, so a hit in the 
    // center will have time "t" at both PMTs
    // the speed of signal travel is C_EFFECTIVE
    // propagte time to the end of the bar
    // column = 0 is a full paddle column ==1,2 is a half paddle

    float tnorth = (column == 2) ? 0 : t + dxnorth/C_EFFECTIVE;
    float tsouth = (column == 1) ? 0 : t + dxsouth/C_EFFECTIVE;
    
    // calculate energy seen by PM for this track step using attenuation factor
    float dEnorth = (column == 2) ? 0 : dEsum * exp(-dxnorth/ATTEN_LENGTH);
    float dEsouth = (column == 1) ? 0 : dEsum * exp(-dxsouth/ATTEN_LENGTH);
    
    int mark = (plane<<20) + (row<<10) + column;
    void** twig = getTwig(&forwardTOFTree, mark);
    
    if (*twig == 0) { // this paddle has not been hit yet by any particle track 
                      // get space and store it

      s_ForwardTOF_t* tof = *twig = make_s_ForwardTOF();
      s_FtofCounters_t* counters = make_s_FtofCounters(1);
      counters->mult = 1;
      counters->in[0].plane = plane;
      counters->in[0].bar = row;
      northHits = HDDM_NULL;
      southHits = HDDM_NULL;
      noMCHits = HDDM_NULL;
      soMCHits = HDDM_NULL;

      // get space for the left/top or right/down PMT data for a total
      // of MAX_HITS possible hits in a single paddle
      // and space for up to MAX_PAD_HITS hits in a paddle to store MC track information 
      // Note: column=0 means paddle read out on both ends coumn=1or2 means single ended readout

      if (column == 0 || column == 1) {
	counters->in[0].ftofNorthTruthHits = northHits
	  = make_s_FtofNorthTruthHits(MAX_HITS);
      }
      if (column == 0 || column == 2) {
	counters->in[0].ftofSouthTruthHits = southHits
	  = make_s_FtofSouthTruthHits(MAX_HITS);
      }

      tof->ftofCounters = counters;
      counterCount++;

    } else { 

      // this paddle is already registered (was hit before)
      // get the hit list back
      s_ForwardTOF_t* tof = *twig;
      northHits = tof->ftofCounters->in[0].ftofNorthTruthHits;
      southHits = tof->ftofCounters->in[0].ftofSouthTruthHits;
      
    }
    
    if (northHits != HDDM_NULL) {
      
      // loop over hits in this PM to find correct time slot
      
      for (nhit = 0; nhit < northHits->mult; nhit++) {
	
	if (fabs(northHits->in[nhit].t - t) < TWO_HIT_RESOL) {
	  break;
	}
      }
      
      // this hit is within the time frame of a previous hit
      // combine the times of this weighted by the energy of the hit
      
      if (nhit < northHits->mult) {         /* merge with former hit */
	northHits->in[nhit].t = 
	  (northHits->in[nhit].t * northHits->in[nhit].dE 
	   + tnorth * dEnorth)
	  / (northHits->in[nhit].dE += dEnorth);
		
	// now add MC tracking information 
	// first get MC pointer of this paddle

	noMCHits = northHits->in[nhit].ftofMCHits;
	unsigned int nMChit = noMCHits->mult;
	if (nMChit<MAX_PAD_HITS) {
	  noMCHits->in[nMChit].x = x[0];
	  noMCHits->in[nMChit].y = x[1];
	  noMCHits->in[nMChit].z = x[2];
	  noMCHits->in[nMChit].E = pin[3];
	  noMCHits->in[nMChit].px = pin[0]*pin[4];
	  noMCHits->in[nMChit].py = pin[1]*pin[4];
	  noMCHits->in[nMChit].pz = pin[2]*pin[4];
	  noMCHits->in[nMChit].ptype = ipart;
	  noMCHits->in[nMChit].itrack = track;
	  noMCHits->in[nMChit].dist = dist;
	  noMCHits->mult++;
	}
	
      }  else if (nhit < MAX_HITS){  // hit in new time window
	northHits->in[nhit].t = tnorth;
	northHits->in[nhit].dE = dEnorth;
	northHits->mult++;

	// create memory for MC track hit information
	northHits->in[nhit].ftofMCHits = noMCHits = make_s_FtofMCHits(MAX_PAD_HITS);

	noMCHits->in[0].x = x[0];
	noMCHits->in[0].y = x[1];
	noMCHits->in[0].z = x[2];
	noMCHits->in[0].E = pin[3];
	noMCHits->in[0].px = pin[0]*pin[4];
	noMCHits->in[0].py = pin[1]*pin[4];
	noMCHits->in[0].pz = pin[2]*pin[4];
	noMCHits->in[0].ptype = ipart;
	noMCHits->in[0].itrack = track;
	noMCHits->in[0].dist = dist;
	noMCHits->mult = 1;
	
      } else {
	fprintf(stderr,"HDGeant error in hitForwardTOF (file hitFTOF.c): ");
	fprintf(stderr,"max hit count %d exceeded, truncating!\n",MAX_HITS);
      }
    }
    
    if (southHits != HDDM_NULL) {
      
      // loop over hits in this PM to find correct time slot
      
      for (nhit = 0; nhit < southHits->mult; nhit++) {
	
	if (fabs(southHits->in[nhit].t - t) < TWO_HIT_RESOL) {
	  break;
	}
      }
      
      // this hit is within the time frame of a previous hit
      // combine the times of this weighted by the energy of the hit
      
      if (nhit < southHits->mult) {         /* merge with former hit */
	southHits->in[nhit].t = 
	  (southHits->in[nhit].t * southHits->in[nhit].dE 
	   + tsouth * dEsouth)
	  / (southHits->in[nhit].dE += dEsouth);
	
	soMCHits = southHits->in[nhit].ftofMCHits;

	// now add MC tracking information 
	unsigned int nMChit = soMCHits->mult;	
	if (nMChit<MAX_PAD_HITS) {
	  
	  soMCHits->in[nMChit].x = x[0];
	  soMCHits->in[nMChit].y = x[1];
	  soMCHits->in[nMChit].z = x[2];
	  soMCHits->in[nMChit].E = pin[3];
	  soMCHits->in[nMChit].px = pin[0]*pin[4];
	  soMCHits->in[nMChit].py = pin[1]*pin[4];
	  soMCHits->in[nMChit].pz = pin[2]*pin[4];
	  soMCHits->in[nMChit].ptype = ipart;
	  soMCHits->in[nMChit].itrack = track;
	  soMCHits->in[nMChit].dist = dist;
	  soMCHits->mult++;
	}
	
      }  else if (nhit < MAX_HITS){  // hit in new time window
	southHits->in[nhit].t = tsouth;
	southHits->in[nhit].dE = dEsouth;
	southHits->mult++;

	// create memory space for MC track hit information
	southHits->in[nhit].ftofMCHits = soMCHits = make_s_FtofMCHits(MAX_PAD_HITS);

	soMCHits->in[0].x = x[0];
	soMCHits->in[0].y = x[1];
	soMCHits->in[0].z = x[2];
	soMCHits->in[0].E = pin[3];
	soMCHits->in[0].px = pin[0]*pin[4];
	soMCHits->in[0].py = pin[1]*pin[4];
	soMCHits->in[0].pz = pin[2]*pin[4];
	soMCHits->in[0].ptype = ipart;
	soMCHits->in[0].itrack = track;
	soMCHits->in[0].dist = dist;
	soMCHits->mult = 1;
	
      } else {
	fprintf(stderr,"HDGeant error in hitForwardTOF (file hitFTOF.c): ");
	fprintf(stderr,"max hit count %d exceeded, truncating!\n",MAX_HITS);
      }
    }
  }
}

/* entry point from fortran */

void hitforwardtof_ (float* xin, float* xout,
                     float* pin, float* pout, float* dEsum,
                     int* track, int* stack, int* history, int* ipart)
{
   hitForwardTOF(xin,xout,pin,pout,*dEsum,*track,*stack,*history, *ipart);
}


/* pick and package the hits for shipping */
// this function is called by loadoutput() (coded in hddmOutput.c)
// which in turn is called by GUOUT at the end of each event

s_ForwardTOF_t* pickForwardTOF ()
{
  s_ForwardTOF_t* box;
  s_ForwardTOF_t* item;
  
  if ((counterCount == 0) && (pointCount == 0))
    {
      return HDDM_NULL;
    }
  
  box = make_s_ForwardTOF();
  box->ftofCounters = make_s_FtofCounters(counterCount);
  box->ftofTruthPoints = make_s_FtofTruthPoints(pointCount);
  
  while (item = (s_ForwardTOF_t*) pickTwig(&forwardTOFTree)){
    s_FtofCounters_t* counters = item->ftofCounters;
    int counter;
    s_FtofTruthPoints_t* points = item->ftofTruthPoints;
    int point;
    
    for (counter=0; counter < counters->mult; ++counter) {
      s_FtofNorthTruthHits_t* northHits = counters->in[counter].ftofNorthTruthHits;
      s_FtofSouthTruthHits_t* southHits = counters->in[counter].ftofSouthTruthHits;
      
      /* compress out the hits below threshold */
      // cut off parameter is THRESH_MEV
      int iok,i;
      int mok=0;
      // loop over all hits in a counter for the left/up PMT
      for (iok=i=0; i < northHits->mult; i++) {
	
	// check threshold
	if (northHits->in[i].dE >= THRESH_MEV/1e3) {
	  
	  if (iok < i) {
	    northHits->in[iok] = northHits->in[i];
	  }
	  ++mok;
	  ++iok;
	}
      }
      
      if (iok) {
	northHits->mult = iok;
      } else if (northHits != HDDM_NULL){ // no hits left over for this PMT	    
	counters->in[counter].ftofNorthHits = HDDM_NULL;
	FREE(northHits);
      }
      
      // now same loop for the right/bottom PMT of a paddle
      for (iok=i=0; i < southHits->mult; i++){
	
	if (southHits->in[i].dE >= THRESH_MEV/1e3){	      
	  if (iok < i){
	    southHits->in[iok] = southHits->in[i];
	  }
	  ++mok;
	  ++iok;
	}
      }
      
      if (iok){
	southHits->mult = iok;
      }
      else if (southHits != HDDM_NULL) {
	counters->in[counter].ftofSouthHits = HDDM_NULL;
	FREE(southHits);
      }
      if (mok){ // total number of time independent FTOF hits in this counter
	int m = box->ftofCounters->mult++;
	// add the hit list of this counter to the list
	box->ftofCounters->in[m] = counters->in[counter];
      }
    } // end of loop over all counters
    
    if (counters != HDDM_NULL) {
      FREE(counters);
    }
    
    // keep also the MC generated primary track particles
    for (point=0; point < points->mult; ++point) {
      int m = box->ftofTruthPoints->mult++;
      box->ftofTruthPoints->in[m] = points->in[point];
    }
    if (points != HDDM_NULL) {
      FREE(points);
    }
    FREE(item);
  }
  
  // reset the counters
  counterCount = pointCount = 0;

  // free the hit list memory used by this event
  if ((box->ftofCounters != HDDM_NULL) &&
      (box->ftofCounters->mult == 0)) {
    FREE(box->ftofCounters);
    box->ftofCounters = HDDM_NULL;
  }
  if ((box->ftofTruthPoints != HDDM_NULL) &&
      (box->ftofTruthPoints->mult == 0)) {
    FREE(box->ftofTruthPoints);
    box->ftofTruthPoints = HDDM_NULL;
  }
  if ((box->ftofCounters->mult == 0) &&
      (box->ftofTruthPoints->mult == 0)) {
    FREE(box);
    box = HDDM_NULL;
  }
  return box;
}

