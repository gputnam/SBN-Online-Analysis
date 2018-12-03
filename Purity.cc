#include "Purity.hh"

/*Adrian Orea: 2018
using Dominic's code and 
https://cdcvs.fnal.gov/redmine/projects/lardbt/repository/revisions/master/entry/LArIATAnaModule/PurityOnlineT1034_module.cc
as references*/

using namespace std;
  // Function to calculate the electron lifetime from a collection of hits
  // You have a vector of recob::Hit objects (you can google recob::Hit to find 
  // out what info is stored in them)

//THIS IS FROM THE LArIAT CODE. It defines the likelihood estimator function.
double likely(const double *par){
	   // Numeric constants
	   double mpshift  = -0.22278298;       				// Landau maximum 
	   int index = 0;
	   const double tau=par[0];
	   const double sigma=par[1];
	   const double dqdxo=par[2];
	   double mpc;             						// Corrected most probable value
	   double dqdx;            						// charge after tau scaling
	   double prob;            						// Landau probability
	   double lik;             						// likelihood
	   double aa;
	   double bb;
	   vector<double> charge;
	   vector<double> dtime;
	   ifstream file ("tmpdontcareaboutthis.txt");
	   while (file.good() && !file.eof())
	   {
	      file >> aa >> bb;
	      dtime.push_back(aa);
	      charge.push_back(bb); 
	   }
	   index=dtime.size()-1;
	   dtime.resize(index);
	   charge.resize(index); 
	   lik=0;
	   for (int ii = 0; ii<index; ++ii)              // Loop over hits
	   { 
	      dqdx=dqdxo*TMath::Exp(-(dtime.at(ii)/tau));
	      mpc = dqdx-mpshift*sigma;                      
	      prob = TMath::Landau(charge.at(ii),mpc,sigma,kTRUE); 
	      lik-=TMath::Log(prob);
	   }     // loop over hits   
	   
	   file.close();
	   dtime.clear();
	   charge.clear();     
	   return lik;
	}

double daqAnalysis::CalculateLifetime(std::vector<art::Ptr<recob::Hit>> rawhits, const struct Analysis::AnalysisConfig& _config){	
	FILE *values_file;							//File where the minimization points are stored
	FILE *taulengthangle;							//File with the Tau values, track length, and angle
	FILE *Collection;							//Collection Plane
	FILE *Induct;								//Collection Plane
	ROOT::Math::Functor f(&likely,3);					//Creates the function based on the Likelihood in likely function
	ROOT::Math::Minimizer* min = ROOT::Math::Factory::CreateMinimizer("Minuit2", "Migrad");	//Initializes the object to minimize
	geo::GeometryCore const* geom = lar::providerFrom<geo::Geometry>();	//Looks at geometry of the hit
	double lowtaulimit;//fcl     						// lower limit on tau minimization
	double hitaulimit;//fcl      						// upper limit on tau minimization
	double lowsigmalimit;//fcl   						// lower limit on sigma minimization
	double hisigmalimit;//fcl    						// upper limit on sigma minimization
	double lowdqdxolimit;//fcl   						// lower limit on dqdxo minimization
	double hidqdxolimit;//fcl    						// upper limit on dqdxo minimization
	double FirstSig = _config.FirstSig;//fcl
	double SecondSig = _config.SecondSig;//fcl
	float singPTime;
	int singWire;
	float IsingPTime;
	int IsingWire;
	int mincount= _config.mincount;//fcl
	int minuniqcount=_config.minuniqcount;//fcl
	int rhsize=rawhits.size();
	

	bool Plot = _config.LifetimePlots;
	bool Dom = _config.Anglecut;
	bool fVerbose = _config.verbose;

	if (rhsize < mincount){
		if(fVerbose) cout << "Minimum Hits of " << mincount << " not met. Skipping event..." << endl;
			;
	}
	
	if(fVerbose) cout << "Amount of hits in event is: " << rhsize << ". Passed the minimum hit cut of: " << mincount << endl;
	
										//Sets vectors for hits/information we need.
	vector<float> PTime, Collect, Unique, Integ, IPTime, Induction, New_Integ, New_PTime, New_Collect,Normal_Integ, INew_Induction, INew_PTime,FitTrialTime, FitTrialCharge, FinalCharge, FinalTime;

	//Check the PCA Value.                                                                         
	TPrincipal *pca = new TPrincipal(2,"");
        double hits[2];

	Collect.clear(); PTime.clear(); Integ.clear(); Induction.clear(); IPTime.clear();
	Collection = fopen("Collection","a");
	Induct = fopen("Induct","a");
	for (int i=0; i<rhsize; i++){
		int channel = rawhits[i]->Channel();				//Gets the channel of the specific hit
		geo::SigType_t sigType = geom->SignalType(channel);		//Stores geometry of the hit
		if(sigType == geo::kCollection){				//Checks for Collection plane
			singWire = (*rawhits[i]).WireID().Wire;
			singPTime = (*rawhits[i]).PeakTime();
			Collect.push_back(singWire);				//WireID
			PTime.push_back(singPTime);				//PTime
			Integ.push_back((*rawhits[i]).Integral());		//Charge
			fprintf(Collection,"%i %6.2f\n",singWire,singPTime);
			//fprintf(Collection,"%d %6.2f\n",2,10.00);

			// Add PCA elements 
			hits[0] = singWire;
			hits[1] = singPTime;
			pca->AddRow(hits);
		}
		if(sigType == geo::kInduction){					//Checks for Induction plane
			IsingWire = (*rawhits[i]).WireID().Wire;
			IsingPTime = (*rawhits[i]).PeakTime();
			Induction.push_back(IsingWire);				//WireID for Induction Plane
			IPTime.push_back(IsingPTime);				//PTime for Induction Plane
			fprintf(Induct,"%i %6.2f\n",IsingWire,IsingPTime);
			//fprintf(Induct,"%d %6.2f\n",3,15.00);
		}
	}
	//fprintf(Collection,"%i %6.2f\n",123,12.34);
	//fprintf(Induct,"%i %6.2f\n",123,12.34);
	fclose(Collection); 							//Collecting Values needed (Charge, WireID, PeakTime)
	fclose(Induct);
	
	int n = Collect.size(); 
	int nI = Induction.size();

	if(fVerbose) cout << "Size of Collect vector is: " << n << endl;			//Size of Collection hits	
	if(fVerbose) cout << "Size of Induction vector is: " << nI << endl;			//Size of Induction hits

	if(n==0){
		if(fVerbose) cout << "No collection plane hits. Skipping event..." << endl;
		return -1;
	}
	if(nI==0){
		if(fVerbose) cout << "No induction plane hits. Skipping event..." << endl;
		return -1;
	}

	Unique.clear();
	Unique.push_back(Collect[0]);						//Unique vector to store only 1 of each hit
	int uniqval = 1;							//Initialize to first element to compare to
	for(int b = 1;b < n; b++){						//Value is unique unless found otherwise
		bool uniq = true;
		for(int c = 0; c < uniqval; c++){
			if(Collect[b]==Unique[c]){
			uniq = false;
	  		}
		}
		if(uniq){							//If the value is unique then it adds it to the Unique vector
			Unique.push_back(Collect[b]);
			uniqval = Unique.size();
		}
	}
	if(uniqval < minuniqcount){						//Minimum Collecection unique hit # cut
		if(fVerbose) cout << "Minimum Unique Hits of " << minuniqcount << " not met. Skipping event..." << endl;
		return -1;
	}
	
	if(fVerbose) cout << "Amount of unique collection plane hits in event is: " << uniqval << ". Passed the minimum unique hit cut of: " << minuniqcount << endl;

	//Find the PCA Eigenvalue and check cut. 
	pca->MakePrincipals();
        const TVectorD *Eigenvalues = pca->GetEigenValues();
	float FirstEigenvalue = (*Eigenvalues)[0];
	delete pca;
	if(fVerbose){std::cout << "PCA Value: " << TMath::Log10(1-FirstEigenvalue) << std::endl;}
	if(TMath::Log10(1-FirstEigenvalue) > _config.pcacut){
	  if(fVerbose){std::cout<< "PCA cut applied" << std::endl;}
	  return -1;
	}

	TGraph *WvT = new TGraph(n,&Collect[0],&PTime[0]);			//Two graphs for Wire and PTime
	TGraph *IWvT = new TGraph(nI,&Induction[0],&IPTime[0]);			//Need the & and [0] notation in order for TGraph to take a vector instead of an array
	
	TF1 *fFunc = new TF1("Linear","[0]*x+[1]", 0, 240);			//(Collection)Two parameters are slope and y-intercept.
	TF1 *fIFunc = new TF1("ILinear","[0]*x+[1]", 0, 240);		//(Induction)Two parameters are slope and y-intercept

	fFunc->SetParameters(1.0,1.0);						//Setting slope and intercept to 1
	fIFunc->SetParameters(1.0,1.0);
	if(false) cout << "Initialized parameters for fits on Wvt and IWvt" << endl;
	
	WvT->Fit("Linear","+Qrob = 0.75");							//Does the fitting for Collection plane
	IWvT->Fit("ILinear","+Qrob = 0.75");						//Does the fitting for Induction plane
	
	//Collection Plane
	Double_t chisqr = fFunc->GetChisquare()/fFunc->GetNDF();  		// Obtain chi^2 divided by number of degrees of freedom
	if(chisqr!=chisqr){
		if(fVerbose) cout << "Chisqr not met..." << endl; 
		return -1;                   				// Skip the event if the chi square is a nan value
	}

	float slope = fFunc->GetParameter(0);
	float yint = fFunc->GetParameter(1);
	if(fVerbose) cout << "First Collection slope: " << slope << "\n First Collection y-intercept: " << yint << endl;

	float Islope = fIFunc->GetParameter(0);
	float Iyint = fIFunc->GetParameter(1);
	if(fVerbose) cout << "First Induction slope: " << Islope << "\n First Induction y-intercept: " << Iyint << endl;

	float fitdev = 2;							//Originally a set distance, now just a percentage.
	double fitvalue;
	int Newn;
	int INewn;
	TGraph *New_WvT;
	TGraph *INew_WvT;

	TH1D *h2 = new TH1D("h2","Distance from fit", 250, 0, 20); 
	TH1D *h3 = new TH1D("h3","Distance from fit", 250, 0, 20);

	//Collection
	for(int z=0; z<2; z++){					  		// make fit twice to make sure to pick up every hit
		New_PTime.clear();
		New_Integ.clear();
		New_Collect.clear();
		for(int i=0; i< n; i++){  					//select only hits close to the fit
	 		fitvalue = slope*Collect[i]+yint;
			h2->Fill(abs(PTime[i]-fitvalue));
	 		if(abs((PTime[i]-fitvalue))<fitdev){
	    			New_PTime.push_back(PTime[i]);
	    			New_Integ.push_back(Integ[i]);
	   			New_Collect.push_back(Collect[i]);
	 		}
		}
		Newn = New_Integ.size();
		New_WvT = new TGraph(Newn,&New_Collect[0],&New_PTime[0]);
		New_WvT->Fit("Linear","+Qrob = 0.75");
	}
	slope = fFunc->GetParameter(0);						//Updating the slope value for the new fit
	if(fVerbose) cout << "The size of NewPT is: " << Newn << endl;
	if(fVerbose) cout << "Refitted Collection slope is: " << slope << endl;
	if(fVerbose) cout << "Chi2/NOF: " << fFunc->GetChisquare()/fFunc->GetNDF()  << endl;

	// Cut on minimum chi2

	if(fFunc->GetChisquare()/fFunc->GetNDF() > _config.chi2cut){
	  if(fVerbose) cout << "Bad chi square. Skipping event...\n";
	return -1;
	}

	//Induction
	for(int q=0; q<2; q++){					  		// make fit twice to make sure to pick up every hit
		INew_PTime.clear();
		INew_Induction.clear();
		for(int i=0; i< nI; i++){  					//select only hits close to the fit
	 		fitvalue = Islope*Induction[i]+Iyint;
			h3->Fill(abs(IPTime[i]-fitvalue));
	 		if(abs((IPTime[i]-fitvalue))<fitdev){
	    			INew_PTime.push_back(IPTime[i]);
	   			INew_Induction.push_back(Induction[i]);
	 		}
		}
		INewn = INew_PTime.size();
		INew_WvT = new TGraph(INewn,&INew_Induction[0],&INew_PTime[0]);
		INew_WvT->Fit("ILinear","+Qrob = 0.75");
	} 
	Islope = fIFunc->GetParameter(0);					//Updating the slope value for the new fit
	if(fVerbose) cout << "The size of NewIPT is: " << INewn << endl;
	if(fVerbose) cout << "Refitted Induction slope is: " << Islope << endl;

										//Pretty much all of the parts prior to fitting from here are from Dom's code
 	float shaping_time = _config.shapingtime;				//In microseconds. Given by Tom
	float drift_vel =  1.50638;						//In mm/microsecond. Calculated by Tom
	float wire_spacing = 4;							//In mm. Assumed
	float angle = atan((shaping_time*drift_vel)/wire_spacing);
	if(_config.fforceanglecut){angle =(_config.anglecut*3.141/180);}
	float us2tick = 0.5;
	float tick_to_wire = us2tick/wire_spacing*drift_vel;			//From Dom's code. Not explicitly in Tom's email...
	slope *= tick_to_wire;
	Islope*= tick_to_wire;
	if(fVerbose) cout << "Converted Slope " << slope << endl;
	if(fVerbose) cout << "Converted ISlope " << Islope << endl;
	float theta = atan(abs(slope));		
	float phi = atan(abs(slope*0.866/Islope));
	if(fVerbose) cout << "The value of theta is: " << theta*180/3.141 << endl;
	if(fVerbose) cout << "The value of phi is: " << phi*180/3.141 << endl;
	
	if(Dom){
		if(theta > angle){			//In Dom's code
			cout << "Dom's Angle cut. Skipping event..." << endl;
			return 1;
		}
	}
		
	float normalization = abs(cos(theta)*cos(phi));				//This is part of the normalization factor for the charges	
	if(fVerbose) cout << "normalisation is : " << normalization <<endl;
	Normal_Integ.resize(Newn);						//Making the vector the same size as New_Integ
	transform(New_Integ.begin(), New_Integ.end(), Normal_Integ.begin(),bind(multiplies<float>(),std::placeholders::_1,normalization*(1/(sqrt(6.28319)*shaping_time*us2tick))));
	if(false) cout << "Normalized the Charge Vector"  << endl;									//^^^^^^^Normalization^^^^^^^
	TGraph *NormalIvT = new TGraph(Newn, &New_PTime[0],&Normal_Integ[0]); 	//Graph like IvT but normalized
	if(false) cout << "Created graph of Normalized charge" << endl;
	double NewPTminx = *min_element(begin(New_PTime),end(New_PTime));	//Getting min of the Collection PTime hits
	if (false) cout << "Found the Minimum of NewPT" << endl;
	double NewPTmaxx = *max_element(begin(New_PTime),end(New_PTime));	//Getting max of the Collection PTime hits
	if(false) cout << "Minimum New PT is: " << NewPTminx << " Maximum New PT is: " << NewPTmaxx << endl;
	TF1 *fNewFunc = new TF1("New_Int","expo", NewPTminx, NewPTmaxx);	//Exponential plot for fitting NormalIvT
	NormalIvT->Fit("New_Int","NQ");						//Fitting NormalIvT with fNewFunc
	if(false) cout << "Fitted the normalized charge graph" << endl;
	double expo0 = fNewFunc->GetParameter(0);				//Getting the "Normalization constant"
	double expo1 = fNewFunc->GetParameter(1);				//Decay rate
	if(fVerbose) cout << "The first 'normalization' constant is: " << expo0 << endl;
	if(fVerbose) cout << "The first decay rate is: " << expo1 << endl;

	TH1D *h1 = new TH1D("h1","Charge Distance", 200, -600, 600); 		//Pretty much a copy and paste from here with my code
	for(int y = 0; y<Newn; y++){
		h1->Fill(Normal_Integ[y]-exp(expo0+(expo1*New_PTime[y])));
	}
	
	TF1 *gfun = new TF1("gfun","landau", -600, 600);			//From LArIAT code
	h1->Fit("gfun","NQ");							//Fitting an Landau to the histogram
	double landau1=gfun->GetParameter(2);					//Sigma of the Landau distribution
	if(fVerbose) cout << "Sigma of the first Landau fit is: " << landau1 << endl;

	double lowcut;
	double hicut;
	TGraph *FitRe;
//THIS IS THE PART THAT ACTUALLY MAKES THE CUT					//This part will refit the exponential between +/- #sigma
	int FitRen;
	for(int r=0; r<2;r++){
		FitTrialCharge.clear();
		FitTrialTime.clear();
		h1->Reset();
		if(true){ 						//Charge should decay
			if(fVerbose) cout << "Cleared the two FitTrial vectors" << endl;
			for (int i = 0; i<Newn; i++){

				if((4*landau1)<1200){
					hicut=exp(expo0+(expo1*New_PTime[i]))+(FirstSig*landau1);
					lowcut=0;
				}
				else{ 
					lowcut=exp(expo0+(expo1*New_PTime[i]))-1200;
					hicut=exp(expo0+(expo1*New_PTime[i]))+1200;
				}
				if(Normal_Integ[i] > lowcut && Normal_Integ[i] < hicut){
					FitTrialCharge.push_back(Normal_Integ[i]);
					FitTrialTime.push_back(New_PTime[i]);
				
				}
			}
			FitRen = FitTrialTime.size();
			if(fVerbose) cout << "The size of FitTrialTime is " << FitRen << endl;
			FitRe = new TGraph(FitRen,&FitTrialTime[0],&FitTrialCharge[0]);
			FitRe->Fit("New_Int","NQ");
			expo0 = fNewFunc->GetParameter(0);			//Getting the "Normalization constant"
			expo1 = fNewFunc->GetParameter(1);			//Decay rate
			for(int y=0; y<FitRen; y++){
				h1->Fill(FitTrialCharge[y]-exp(expo0+(expo1*FitTrialTime[y])));
			}
			h1->Fit("gfun","NQ");
			landau1=gfun->GetParameter(2);
		}
		/*else{
			if(fVerbose) cout << "The first exponential rate is positive. Skipping event..." << endl;
			return -1;
		}*/
	}
	if(fVerbose) cout << "The refitted 'normalization' constant is: " << expo0 << endl;
	if(fVerbose) cout << "The refitted decay rate is: " << expo1 << endl;
	if(fVerbose) cout << "Sigma of the Refitted Landau fit is: " << landau1 << endl;

//THIS PART CHANGES THE ACTUAL CUT
	TGraph *FinalFit;
	int Finaln;
	if(expo1<0) expo1 *=-1;
	for(int u=0;u<2;u++){
		 //expo1 < 0.0	
		if(true){ 						//Charge should decay
			FinalCharge.clear();
			FinalTime.clear();
			h1->Reset();
			values_file=fopen("tmpdontcareaboutthis.txt","w");
			for (int i = 0; i<FitRen; i++){
				if((4*landau1)<1200){
					hicut	=exp(expo0+(expo1*FitTrialTime[i]))+(SecondSig*landau1);
					lowcut=0;
				}
				else{ 
					lowcut=exp(expo0+(expo1*FitTrialTime[i]))-1200;
					hicut=exp(expo0+(expo1*FitTrialTime[i]))+1200;
				}
				if(FitTrialCharge[i] > lowcut && FitTrialCharge[i] < hicut){
					FinalCharge.push_back(FitTrialCharge[i]);
					FinalTime.push_back(FitTrialTime[i]);
					fprintf(values_file,"%6.2f %6.0f\n",FitTrialTime[i],FitTrialCharge[i]);
				}
			}
			Finaln = FinalTime.size();
			FinalFit = new TGraph(Finaln,&FinalTime[0],&FinalCharge[0]);
			FinalFit->Fit("New_Int","NQ");
			expo0 = fNewFunc->GetParameter(0);			//Getting the "Normalization constant"
			expo1 = fNewFunc->GetParameter(1);			//Decay rate
			for(int y=0; y<Finaln; y++){
				h1->Fill(FinalCharge[y]-exp(expo0+(expo1*FinalTime[y])));
			}
			h1->Fit("gfun","NQ");
			landau1=gfun->GetParameter(2);
			fclose(values_file);
		}
		/*else{
			if(fVerbose) cout << "The second exponential rate is positive. Skipping event..." << endl;
			return -1;
		}*/
	}
	cout << "Final decay constant and normalization constant are: " << expo1 << " , " << expo0 << endl;
	float lifetime = 1/(-1*expo1);						//The lifetime of the exponential fit
	double icharge = exp(expo0);						//Initial charge dQ/dx0
	TF1 *sigma = new TF1("sigma","[0]*[1]",NewPTminx, NewPTmaxx);
	sigma->SetParameter(0,FirstSig);
	sigma->SetParameter(1,landau1);
	TF1 *fsum = new TF1("fsum","sigma + New_Int",NewPTminx,NewPTmaxx);
	if(fVerbose) cout << "landau1: " << landau1 << " Firstsig: " << FirstSig << endl;
	if(fVerbose) cout << "All calculations are made. Ready for Minimization" << endl;

////////////////////////////////////////////////////////////////////
////////////////////// JUST PLOTTING STUFF /////////////////////////
////////////////////////////////////////////////////////////////////
	
	if(Plot){
		TCanvas *cWvT  = new TCanvas("WvT","Peaktime vs WireID",10,10,800,600); 	//Creating canvases to draw onto. Purely for visual inspection
		TCanvas *cIWvT  = new TCanvas("IWvT","Peaktime vs WireID",10,10,800,600);
		TCanvas *cInt  = new TCanvas("Int","Integral vs PeakTime",10,10,800,600);
		TCanvas *cHists  = new TCanvas("PtimeDev","Distance from fit",10,10,800,600);
		TCanvas *cLandau  = new TCanvas("Landau","Distance of charge from fit",10,10,800,600);
		cWvT->Divide(1,2);
		cWvT->cd(1);
			gStyle->SetOptStat(0);
			WvT->SetMarkerStyle(3);
			WvT->SetTitle("Collection Plane");
			WvT->GetXaxis()->SetTitle("WireID");
			WvT->GetYaxis()->SetTitle("PeakTime(tick)");
			WvT->GetXaxis()->SetLimits(0,240);
			WvT->Draw("AP"); 							//Data.No Connecting Line
			fFunc->Draw("SAME");
		cWvT->cd(2);
			New_WvT->SetMarkerColor(kBlue);
			New_WvT->SetMarkerStyle(4);
			New_WvT->SetTitle("Collection Plane");
			New_WvT->GetXaxis()->SetTitle("WireID");
			New_WvT->GetYaxis()->SetTitle("PeakTime(tick)");
			New_WvT->GetXaxis()->SetLimits(0,240);
			New_WvT->Draw("AP");
			fFunc->Draw("SAME");

		cWvT->SaveAs("Peak.root");
		
		cIWvT->Divide(1,2);
		cIWvT->cd(1);
			gStyle->SetOptStat(0);
			IWvT->SetMarkerStyle(3);
			IWvT->SetTitle("Induction Plane");
			IWvT->GetXaxis()->SetTitle("WireID");
			IWvT->GetYaxis()->SetTitle("PeakTime(tick)");
			IWvT->GetXaxis()->SetLimits(0,240);
			IWvT->Draw("AP"); 							//Data. No Connecting Line
			fIFunc->Draw("SAME");
		cIWvT->cd(2);
			INew_WvT->SetMarkerColor(kBlue);
			INew_WvT->SetMarkerStyle(4);
			INew_WvT->SetTitle("Induction Plane");
			INew_WvT->GetXaxis()->SetTitle("WireID");
			INew_WvT->GetYaxis()->SetTitle("PeakTime(tick)");
			INew_WvT->GetXaxis()->SetLimits(0,240);
			INew_WvT->Draw("APSAME");
			fIFunc->Draw("SAME");
		cIWvT->SaveAs("IPeak.root");
		
		cInt->Divide(1,2);
			cInt->cd(1);
				gStyle->SetOptStat(0);
				NormalIvT->SetMarkerColor(kRed);
				NormalIvT->SetMarkerStyle(2);
				NormalIvT->GetYaxis()->SetRangeUser(0,900);
				NormalIvT->SetTitle("Charge; PTime(ticks); Charge (ADCs)");
				NormalIvT->Draw("AP");

				fNewFunc->SetLineColor(kMagenta);
				fNewFunc->Draw("SAME");

				fsum->SetLineColor(kMagenta);
				fsum->Draw("SAME");
			cInt->cd(2);
				FinalFit->SetMarkerColor(kGreen);
				FinalFit->SetMarkerStyle(3);
				FinalFit->GetYaxis()->SetRangeUser(0,900);
				FinalFit->SetTitle("Charge; PTime(ticks); Charge (ADCs)");
				FinalFit->Draw("AP");			
			
				fNewFunc->SetLineColor(kOrange);
				fNewFunc->Draw("SAME");
			cInt->SaveAs("Charge.root");

		cHists->Divide(1,2);
		cHists->cd(1);
			h2->SetLineColor(kBlue);
			h2->SetTitle("Collection Plane; Ticks; Entries");
			h2->GetYaxis()->SetTickLength(.01);
			h2->Draw();
		cHists->cd(2);
			h3->SetLineColor(kBlue);
			h3->SetTitle("Induction Plane; Ticks; Entries");
			h3->GetYaxis()->SetTickLength(.01);
			h3->Draw();
		cHists->SaveAs("Distance.root");
	
		cLandau->cd();
			h1->SetTitle("Charge distance to fit; ADC's; Entries");
			h1->GetYaxis()->SetTickLength(.01);
			h1->Draw();
			gfun->Draw("SAME");
		cLandau->SaveAs("Landau.root");
	}


///////////////////////////////////////////////////////////////////////////
////////////////////// THIS IS FROM THE LArIAT CODE EXACTLY ///////////////
///////////////////////////////////////////////////////////////////////////	

	min->SetFunction(f);
										// set tolerance , etc... This is in the LArIAT code
	min->SetMaxFunctionCalls(100000); 					// for Minuit/Minuit2 
	min->SetMaxIterations(100000); 	 					// for GSL 
	min->SetTolerance(0.0001);
	min->SetPrintLevel(1);
 
										// Set the free variables to be minimized!
	min->SetVariable(0,"Tau",lifetime, 1);					//Originally had fvariable[1] [2] and [3] for Tau, Sigma, and dqdx0 respectively
	min->SetVariable(1,"Sigma",landau1,1);					//I'm just using the values I found for this event since they are just initial values for minimization
	min->SetVariable(2,"dqdx0",icharge,1);					//Also had fstep[1] " " but I just used 1, which is the same as the fstep array I believe
	lowtaulimit= _config.lowtaulimit;                                                       // lower limit on tau minimization
	hitaulimit=_config.hitaulimit;                                                          // upper limit on tau minimization                   
	lowsigmalimit=_config.lowsigmalimit;                                                    // lower limit on sigma minimization
	hisigmalimit=_config.hisigmalimit;                                                      // upper limit on sigma minimization
	lowdqdxolimit=_config.lowdqdxolimit;                                                    // lower limit on dqdxo minimization
	hidqdxolimit=_config.hidqdxolimit;                                                      // upper limit on dqdxo minimization
	min->SetVariableLimits(0,lowtaulimit,hitaulimit);
	min->SetVariableLimits(1,lowsigmalimit,hisigmalimit);
	min->SetVariableLimits(2,lowdqdxolimit,hidqdxolimit);
	min->SetVariableValue(2,icharge);

	try{min->Minimize();} 							// do the minimization
  catch(...){}

	const double *xs = min->X();						//Making sure we actually get values we want
	if( xs[0]!=xs[0] || xs[1]!=xs[1] || xs[2]!=xs[2] ) 
	{
	 if(fVerbose) cout << "MINIMIZATION GAVE NAN VALUE!!!! Skipping the event" << endl; 
	 return -1;
	}

	double tauvec = xs[0];
	if(fVerbose) cout << "LArIAT FUll ANALYSIS LIFETIME IS: " << (tauvec/2000) << "ms" << endl;

	if( xs[0]!=xs[0] || xs[1]!=xs[1] || xs[2]!=xs[2] ) {
	 	if(fVerbose) cout << "MINIMIZATION GAVE NAN VALUE!!!! Skipping the event" << endl; 
	 	return -1;
	}
	// Get Errors from MiNOS
	min->SetErrorDef(0.5);
	for (int l=0; l<3; l++){
		//sigminoserrlow[l] = 0;
		//sigminoserrhi[l] = 0;
	}
	/*min->GetMinosError(0, sigminoserrlow[0], sigminoserrhi[0]);		// Get MINOS errors accordingly.
	min->GetMinosError(1, sigminoserrlow[1], sigminoserrhi[1]); 		// Get MINOS errors accordingly.
	min->GetMinosError(2, sigminoserrlow[2], sigminoserrhi[2]); 		// Get MINOS errors accordingly.*/
	const double *exs = min->Errors();

	if( exs[0]!=exs[0] || exs[1]!=exs[1] || exs[2]!=exs[2] ) 
	{
	 if(fVerbose) cout << "MINIMIZATION GAVE NAN VALUE ON ERRORS!!!! Skipping the event" << endl; 
	 return -1;
	}

	double error_tauvec = exs[0];
	min->Clear();

	double x0 = New_Collect[0]*wire_spacing;				//Converting to mm
	double y0 = New_PTime[0]*0.5*drift_vel;					//Converting to mm
	double xf = New_Collect[Newn-1]*wire_spacing;				//Converting to mm
	double yf = New_PTime[Newn-1]*0.5*drift_vel;				//Converting to mm
	double length = sqrt(pow((xf-x0),2)+pow((yf-y0),2));			//Length in mm
	double Mangle = atan((yf-y0)/(xf-x0))*180/3.14;				//In Degrees
	taulengthangle=fopen("TTLMA","a");
	fprintf(taulengthangle,"%6.2f %6.2f %6.0f %6.2f %6.2f\n",tauvec/2000,length,Mangle,error_tauvec/2000,lifetime/2000);
	fclose(taulengthangle);

	if(fVerbose) cout << "Lifetime (exp) is: " << (lifetime/2000)  << "ms" << endl << endl << endl << endl;

	if(tauvec < 0 || (tauvec/2000) > 24){return -1;}

  return tauvec;
}
