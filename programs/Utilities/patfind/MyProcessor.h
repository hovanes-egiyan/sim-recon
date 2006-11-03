// Author: David Lawrence  June 25, 2004
//
//
// MyProcessor.h
//
/// Example program for a Hall-D analyzer which uses DANA
///


#include "JANA/JEventProcessor.h"
#include "JANA/JEventLoop.h"
#include "DANA/DApplication.h"

#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TTree.h>
#include <TLine.h>
#include <TEllipse.h>
#include <TCanvas.h>
#include <TMarker.h>

#include "TRACKING/Dtrk_hit.h"
#include "TRACKING/DQuickFit.h"

class DMagneticFieldMap;
class DTrackCandidate_factory;

class MyProcessor:public JEventProcessor
{
	public:
		MyProcessor();
		jerror_t init(void);					///< Called once at program start.
		jerror_t evnt(JEventLoop *eventLoop, int eventnumber);	///< Called every event.
		jerror_t fini(void);					///< Called after last event of last event source has been processed.

		jerror_t PlotXYHits(void);
		jerror_t PlotPhiVsZ(void);
		jerror_t PlotPhiZSlope(void);
		jerror_t PlotZVertex(void);
		jerror_t PlotStats(void);
		void DrawXYFit(DQuickFit *fit, int color, int width);
		void DrawCircle(float x0, float y0, float r0, int color, int width);
		void DrawXYDot(Dtrk_hit *hit, float size, int style, int color);
		void DrawXYDots(vector<Dtrk_hit *> hits, float size, int style, int color);
		void DrawPhiZDots(vector<Dtrk_hit *> hits, DQuickFit *fit, float size, int style, int color);
		void DrawPhiZFit(DQuickFit *fit, int color, int width);
		void DrawPhiZLine(float dphidz, float z_vertex, int color, int width);

		TH2F *axes, *axes_phiz, *axes_hits;
		DTrackCandidate_factory *factory;
		JEventLoop *eventLoop;
		
		vector<TObject*> graphics;

		vector<Dtrk_hit*> trkhits;
		vector<vector<Dtrk_hit*> > dbg_in_seed;
		vector<vector<Dtrk_hit*> > dbg_hoc;
		vector<vector<Dtrk_hit*> > dbg_hol;
		vector<vector<Dtrk_hit*> > dbg_hot;
		vector<DQuickFit*> dbg_seed_fit;
		vector<DQuickFit*> dbg_track_fit;
		vector<int> dbg_seed_index;
		vector<TH1F*> dbg_phiz_hist;
		vector<int> dbg_phiz_hist_seed;
		vector<TH1F*> dbg_zvertex_hist;
		vector<int> dbg_zvertex_hist_seed;
		vector<float> dbg_phizangle;
		vector<float> dbg_z_vertex;

};


