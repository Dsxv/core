/**************************************************************************************
Copyright 2015 Applied Research Associates, Inc.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License. You may obtain a copy of the License
at:
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
**************************************************************************************/

#include "HowToTracker.h"

// Include the various types you will be using in your code
#include "biogears/cdm/patient/actions/SESubstanceCompoundInfusion.h"
#include <biogears/cdm/compartment/SECompartmentManager.h>
#include <biogears/cdm/engine/PhysiologyEngineTrack.h>
#include <biogears/cdm/patient/actions/SEAsthmaAttack.h>
#include <biogears/cdm/patient/actions/SEConsumeNutrients.h>
#include <biogears/cdm/patient/actions/SEInfection.h>
#include <biogears/cdm/patient/actions/SESubstanceOralDose.h>
#include <biogears/cdm/properties/SEScalarTypes.h>
#include <biogears/cdm/substance/SESubstanceManager.h>
#include <biogears/cdm/system/physiology/SEBloodChemistrySystem.h>
#include <biogears/cdm/system/physiology/SECardiovascularSystem.h>
#include <biogears/cdm/system/physiology/SERespiratorySystem.h>
#include <biogears/string/manipulation.h>
using namespace biogears;

//Helper expressions to convert the input to minutes
constexpr double weeks(double n_weeks) { return n_weeks * 7. * 24. * 60.; }
constexpr double days(double n_days) { return n_days * 24. * 60.; }
constexpr double hours(double n_hours) { return n_hours * 60.; }
constexpr double minutes(double n_minutes) { return n_minutes; }
constexpr double seconds(double n_seconds) { return n_seconds / 60.; }

constexpr double to_seconds(double n_minutes) { return n_minutes * 60; }
//--------------------------------------------------------------------------------------------------
/// \brief
/// Run a Sepsis scenario with variable paramaters
///
/// \param std::string name - Log file name
/// \param double mic_g_per_l - (gram/litter) Minimal Inhibitory Concentration of the Sepsis Infection (Affects Antibiotic response)
/// \param double apply_at_m - (minutes) how far after the initial infection we want o apply antibiotics
/// \param double application_interval_m - (minutes) How often after the first antibiotics application we reapply antibiotics
/// \param double duration_hr - (hours) Total simulation lenght in hours
/// \details
///   Timeline -> Stablization ->  Apply Infection Moderate with MIC ->  AdvanceTime by apply_at -> apply antibiotics
///   Until duration has passed apply antibiotics every application interval
///
//--------------------------------------------------------------------------------------------------
bool HowToPatientGeneration(std::string name, int severity, double mic_g_per_l, double apply_at_m, double application_interval_m, std::string patient, double duration_hr)
{
  auto eSeverity = CDM::enumInfectionSeverity::value::Eliminated;
  char const* sSeverity = "Eliminated";
  switch (severity) {
  default:
  case 0:
    eSeverity = CDM::enumInfectionSeverity::value::Mild;
    sSeverity = "Mild";
    break;
  case 1:
    eSeverity = CDM::enumInfectionSeverity::value::Moderate;
    sSeverity = "Moderate";
    break;
  case 2:
    eSeverity = CDM::enumInfectionSeverity::value::Severe;
    sSeverity = "Severe";
    break;
  }
  std::string long_name = name + asprintf("-%s-%fg_Per_l-%fm-%fm-%fhr", sSeverity, apply_at_m, application_interval_m, duration_hr);

  // Create the engine and load the patient
  Logger logger(name + ".log");
  std::unique_ptr<
    PhysiologyEngine>
    bg = CreateBioGearsEngine(&logger);
  bg->GetLogger()->Info("Starting " + long_name);
  if (!bg->LoadState("states/StandardMale@0s.xml")) {
    bg->GetLogger()->Error("Could not load state, check the error");
    return false;
  }

  // Create data requests for each value that should be written to the output log as the engine is executing
  // Physiology System Names are defined on the System Objects
  // defined in the Physiology.xsd file
  //Example bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("HeartRate", FrequencyUnit::Per_min);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("HeartRate", FrequencyUnit::Per_min);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("MeanArterialPressure", PressureUnit::mmHg);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("SystolicArterialPressure", PressureUnit::mmHg);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("DiastolicArterialPressure", PressureUnit::mmHg);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("CardiacOutput", VolumePerTimeUnit::L_Per_min);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("HemoglobinContent", MassUnit::g);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("CentralVenousPressure", PressureUnit::mmHg);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("Hematocrit");
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("ArterialBloodPH");
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("UrinationRate", VolumePerTimeUnit::mL_Per_hr);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("WhiteBloodCellCount", AmountPerVolumeUnit::ct_Per_uL);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("UrineProductionRate", VolumePerTimeUnit::mL_Per_min);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("RespirationRate", FrequencyUnit::Per_min);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("OxygenSaturation");
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("CarbonDioxideSaturation");
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("CoreTemperature", TemperatureUnit::C);
  bg->GetEngineTrack()->GetDataRequestManager().CreatePhysiologyDataRequest().Set("SkinTemperature", TemperatureUnit::C);

  bg->GetEngineTrack()->GetDataRequestManager().CreateSubstanceDataRequest().Set(*bg->GetSubstanceManager().GetSubstance("Bicarbonate"), "BloodConcentration", MassPerVolumeUnit::mg_Per_dL);
  bg->GetEngineTrack()->GetDataRequestManager().CreateSubstanceDataRequest().Set(*bg->GetSubstanceManager().GetSubstance("Creatinine"), "BloodConcentration", MassPerVolumeUnit::mg_Per_dL);
  bg->GetEngineTrack()->GetDataRequestManager().CreateSubstanceDataRequest().Set(*bg->GetSubstanceManager().GetSubstance("Lactate"), "BloodConcentration", MassPerVolumeUnit::mg_Per_dL);
  bg->GetEngineTrack()->GetDataRequestManager().CreateSubstanceDataRequest().Set(*bg->GetSubstanceManager().GetSubstance("Piperacillin"), "BloodConcentration", MassPerVolumeUnit::mg_Per_dL);
  bg->GetEngineTrack()->GetDataRequestManager().CreateSubstanceDataRequest().Set(*bg->GetSubstanceManager().GetSubstance("Tazobactam"), "BloodConcentration", MassPerVolumeUnit::mg_Per_dL);

  bg->GetEngineTrack()->GetDataRequestManager().SetResultsFilename(long_name + ".csv");
  bg->GetEngineTrack()->GetDataRequestManager().SetSamplesPerSecond(1. / (5. * 60.));

  HowToTracker tracker(*bg);
  SEInfection infection{};
  infection.SetSeverity(eSeverity);
  SEScalarMassPerVolume infection_mic;
  infection_mic.SetValue(mic_g_per_l, MassPerVolumeUnit::g_Per_L);
  infection.GetMinimumInhibitoryConcentration().Set(infection_mic);
  infection.SetLocation("Gut");
  bg->ProcessAction(infection);
  auto& substances = bg->GetSubstanceManager();

  SESubstanceCompound* PiperacillinTazobactam = bg->GetSubstanceManager().GetCompound("PiperacillinTazobactam");
  SESubstanceCompoundInfusion full_antibiotics_bag{ *PiperacillinTazobactam };
  SESubstanceCompoundInfusion empty_antibiotics_bag{ *PiperacillinTazobactam };
  full_antibiotics_bag.GetBagVolume().SetValue(20.0, VolumeUnit::mL);
  empty_antibiotics_bag.GetBagVolume().SetValue(20.0, VolumeUnit::mL);
  full_antibiotics_bag.GetRate().SetValue(0.667, VolumePerTimeUnit::mL_Per_min);
  empty_antibiotics_bag.GetRate().SetValue(0, VolumePerTimeUnit::mL_Per_min);
  //Load substances we might use

  double time_remaining_min = hours(duration_hr);
  double time_since_feeding_min = 0;
  double time_since_antibiotic_treatment_min = 0;
  double time_applying_antibiotics_min = 0;

  bool first_treatment_occured = false;
  bool applying_antibiotics = true;

  while (0. < time_remaining_min) {
    auto current_time = bg->GetSimulationTime(TimeUnit::min);

    if (time_since_feeding_min > hours(8)) {
      time_since_feeding_min -= hours(8);
      SEConsumeNutrients meal;
      meal.GetNutrition().GetCarbohydrate().SetValue(50, MassUnit::g);
      meal.GetNutrition().GetFat().SetValue(10, MassUnit::g);
      meal.GetNutrition().GetProtein().SetValue(20, MassUnit::g);
      meal.GetNutrition().GetCalcium().SetValue(100, MassUnit::g);
      meal.GetNutrition().GetWater().SetValue(480, VolumeUnit::mL);
      meal.GetNutrition().GetSodium().SetValue(2, MassUnit::g);
      //meal.GetNutrition().GetCarbohydrateDigestionRate().SetValue(50, VolumeUnit::mL);
      //meal.GetNutrition().GetFatDigestionRate().MassUnit::g(50, VolumeUnit::mL);
      //meal.GetNutrition().GetProteinDigestionRate().SetValue(50, VolumeUnit::mL);
      bg->ProcessAction(meal);
    }

    if (first_treatment_occured) {

    } else {
      if (apply_at_m > current_time) {
        bg->ProcessAction(full_antibiotics_bag);
        first_treatment_occured = true;
        applying_antibiotics = true;
      }
    }

    if (time_remaining_min < hours(1)) {
      tracker.AdvanceModelTime(to_seconds(time_remaining_min));
    } else {
      if (applying_antibiotics) {
        auto volume_applied = full_antibiotics_bag.GetRate().GetValue(VolumePerTimeUnit::mL_Per_min) * time_applying_antibiotics_min;

       
        if (volume_applied + full_antibiotics_bag.GetRate().GetValue(VolumePerTimeUnit::mL_Per_min) * hours(1) < full_antibiotics_bag.GetBagVolume().GetValue(VolumeUnit::mL)) {
          //Case : Applying Antibotics and more then an hour of bag left
          tracker.AdvanceModelTime(hours(1));
          time_since_antibiotic_treatment_min = 0;
          time_since_feeding_min += hours(1);
        } else {
          //Case : Applying Antibotics with less then an hour in the bag left
          auto volume_remaining = full_antibiotics_bag.GetBagVolume().GetValue(VolumeUnit::mL) - volume_applied;
          auto time_remaining = volume_remaining / full_antibiotics_bag.GetRate().GetValue(VolumePerTimeUnit::mL_Per_min);
          tracker.AdvanceModelTime(time_remaining);

          time_since_antibiotic_treatment_min = 0;
          time_since_feeding_min += time_remaining;

          bg->ProcessAction(empty_antibiotics_bag);
          applying_antibiotics = false;
        }
      } else {
        if (time_since_antibiotic_treatment_min + hours(1) < application_interval_m) {
          //Not Applying Antibiotics and over an hour until the next application
          tracker.AdvanceModelTime(hours(1));
          time_since_antibiotic_treatment_min += hours(1);
          time_since_feeding_min += hours(1);
        } else {
          auto time_remaining = application_interval_m - time_since_antibiotic_treatment_min;
          tracker.AdvanceModelTime(time_remaining_min);
          applying_antibiotics = true;
        }
      }
    }

    time_remaining_min = hours(duration_hr) - bg->GetSimulationTime(TimeUnit::min);
  }

  return true;
}
