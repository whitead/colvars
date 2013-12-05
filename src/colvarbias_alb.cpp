#include "colvarmodule.h"
#include "colvarbias_alb.h"
#include "colvarbias.h"
#include <stdio.h>


colvarbias_alb::colvarbias_alb(std::string const &conf, char const *key) :
  colvarbias(conf, key), update_calls(0), set_coupling_force(0.), current_coupling_force(0.), coupling_force_accum(0.), b_equilibration(true) {


  // get the initial restraint centers
  colvar_centers.resize (colvars.size());
  
  means.resize(colvars.size());
  means_sq.resize(colvars.size());
  means_cu.resize(colvars.size());

  for (size_t i = 0; i < colvars.size(); i++) {
    colvar_centers[i].type (colvars[i]->type());

    //copy colvars for mean
    means[i] = colvarvalue(colvars[i]->value());
    means_cu[i] = colvarvalue(colvars[i]->value());
    //set them to null
    means[i].reset();
    means_cu[i].reset();
    //zero other moments
    means_sq[i] = 0;

  }
  if (get_keyval (conf, "centers", colvar_centers, colvar_centers)) {
    for (size_t i = 0; i < colvars.size(); i++) {
      colvar_centers[i].apply_constraints();
    }
  } else {
    colvar_centers.clear();
    cvm::fatal_error ("Error: must define the initial centers of adaptive linear bias .\n");
  }

  if (colvar_centers.size() != colvars.size())
    cvm::fatal_error ("Error: number of centers does not match "
                      "that of collective variables.\n");

  if(!get_keyval (conf, "UpdateFrequency", update_freq, 0))
    cvm::fatal_error("Error: must set updateFrequency for apadtive linear bias.\n");
  
  //assume update frequency is twice the correlation time.
  equil_time = (int) (update_freq / 2.) + 1;
  update_freq = 2 * equil_time;
  if(update_freq == 0)
    cvm::fatal_error("Error: must set updateFrequency to greater than 1.\n");

  get_keyval (conf, "outputCenters", b_output_centers, false);
  get_keyval (conf, "outputGradient", b_output_grad, false);
  get_keyval (conf, "outputCoupling", b_output_coupling, true);

  //initial guess
  get_keyval (conf, "forceConstant", set_coupling_force, 0.0);
  
  //how we're going to increase to that point
  coupling_force_rate = (set_coupling_force - current_coupling_force) / equil_time;
  

  if(cvm::temperature() > 0)
    get_keyval (conf, "couplingRange", max_coupling_change, 3 * cvm::temperature() * cvm::boltzmann());
  else
    get_keyval (conf, "couplingRange", max_coupling_change, 3 * cvm::boltzmann());

  if (cvm::debug())
    cvm::log (" bias.\n");

}

colvarbias_alb::~colvarbias_alb() {
  
  if (cvm::n_rest_biases > 0)
    cvm::n_rest_biases -= 1;
}

cvm::real colvarbias_alb::update() {

  bias_energy = 0.0;
  update_calls++;

  if (cvm::debug())
    cvm::log ("Updating the adaptive linear bias \""+this->name+"\".\n");

  

  
  //log the moments of the CVs
  // Force and energy calculation
  for (size_t i = 0; i < colvars.size(); i++) {
    colvar_forces[i] = -restraint_force(restraint_convert_k(current_coupling_force, colvars[i]->width),
					colvars[i],
					colvar_centers[i]);
    bias_energy += restraint_potential(restraint_convert_k(current_coupling_force, colvars[i]->width),
				       colvars[i],
				       colvar_centers[i]);

    if(!b_equilibration) {
      
      //scale down without copying
      means[i] *= (update_calls - 1.) / update_calls;
      means_sq[i] *= (update_calls - 1.) / update_calls;
      means_cu[i] *= (update_calls - 1.) / update_calls;

      
      //add with copy from divide
      means[i] += colvars[i]->value() / static_cast<cvm::real> (update_calls);
      means_sq[i] += colvars[i]->value().norm2() / static_cast<cvm::real> (update_calls);
      means_cu[i] += colvars[i]->value().norm2() * colvars[i]->value() / static_cast<cvm::real> (update_calls);
    }
  }

  if(b_equilibration) {
    current_coupling_force += coupling_force_rate;
    if(update_calls == equil_time) {
      b_equilibration = false;
      update_calls = 0;
    }
  }


  //now we update coupling force, if necessary
  if(update_calls == update_freq) {
    
    //use estimated variance to take a step
    cvm::real step_size = 0;
    cvm::real temp;

    //reset means and means_sq
    for(size_t i = 0; i < colvars.size(); i++) {
      
      //temp = means_cu[i] - means[i] * means_sq[i] - 2. * colvar_centers[i] * means_sq[i] + 2. *
      //colvar_centers[i] * means[i] * means[i];
      temp = 2. * (means[i] - colvar_centers[i]) * (means_sq[i] - means[i] * means[i]);
      
      if(cvm::temperature() > 0)
	step_size += temp / (cvm::temperature()  * cvm::boltzmann());
      else
	step_size += temp / cvm::boltzmann();

      means[i].reset();
      means_sq[i] = 0;
      means_cu[i].reset();
    }
    
    coupling_force_accum += step_size * step_size;

    //    printf("covariance estimate = %f\n", static_cast<double> (means_sq[0] - means[0] * means[0]));
    //    printf("deviation = %f\n", static_cast<double> (means[0] - colvar_centers[0]));
    //    printf("step_size = %f\n", step_size);
    //    printf("sqrt(coupling_accum) = %f\n", sqrt(coupling_force_accum));
    //    printf("max_coupling_change = %f\n", max_coupling_change);
    //    printf("coupling force change = %f\n", max_coupling_change / sqrt(coupling_force_accum) * step_size);

    current_coupling_force = set_coupling_force;
    set_coupling_force += max_coupling_change / sqrt(coupling_force_accum) * step_size;
    coupling_force_rate = (set_coupling_force - current_coupling_force) / equil_time;
      


    update_calls = 0;      
    b_equilibration = true;

  }

  if (cvm::debug())
    cvm::log ("Current forces for the adaptive linear bias \""+
              this->name+"\": "+cvm::to_str (colvar_forces)+".\n");

  return bias_energy;

}


std::istream & colvarbias_alb::read_restart (std::istream &is)
{
  size_t const start_pos = is.tellg();

  cvm::log ("Restarting adaptive linear bias \""+
            this->name+"\".\n");

  std::string key, brace, conf;
  if ( !(is >> key)   || !(key == "ALB") ||
       !(is >> brace) || !(brace == "{") ||
       !(is >> colvarparse::read_block ("configuration", conf)) ) {

    cvm::log ("Error: in reading restart configuration for restraint bias \""+
              this->name+"\" at position "+
              cvm::to_str (is.tellg())+" in stream.\n");
    is.clear();
    is.seekg (start_pos, std::ios::beg);
    is.setstate (std::ios::failbit);
    return is;
  }

  std::string name = "";
  if ( (colvarparse::get_keyval (conf, "name", name, std::string (""), colvarparse::parse_silent)) &&
       (name != this->name) )
    cvm::fatal_error ("Error: in the restart file, the "
                      "\"ALB\" block has a wrong name\n");
  if (name.size() == 0) {
    cvm::fatal_error ("Error: \"ALB\" block in the restart file "
                      "has no identifiers.\n");
  }

  if (!get_keyval (conf, "forceConstant", set_coupling_force))
    cvm::fatal_error ("Error: current force constant  is missing from the restart.\n");

  is >> brace;
  if (brace != "}") {
    cvm::fatal_error ("Error: corrupt restart information for adaptive linear bias \""+
                      this->name+"\": no matching brace at position "+
                      cvm::to_str (is.tellg())+" in the restart file.\n");
    is.setstate (std::ios::failbit);
  }
  return is;
}


std::ostream & colvarbias_alb::write_restart (std::ostream &os)
{
  os << "ALB {\n"
     << "  configuration {\n"
    //      << "    id " << this->id << "\n"
     << "    name " << this->name << "\n";

  os << "    couplingForce "
     << std::setprecision (cvm::en_prec)
     << std::setw (cvm::en_width) << set_coupling_force << "\n";


  os << "  }\n"
     << "}\n\n";

  return os;
}


std::ostream & colvarbias_alb::write_traj_label (std::ostream &os)
{
  os << " ";

  if (b_output_energy)
    os << " E_"
       << cvm::wrap_string (this->name, cvm::en_width-2);

  if (b_output_coupling)
    os << " Alpha_"
       << cvm::wrap_string (this->name, cvm::en_width-6);

  if(b_output_grad)
    for(size_t i = 0; i < means.size(); i++) {
      os << "Grad_"
	 << cvm::wrap_string(colvars[i]->name, cvm::cv_width - 4);
    }

  if (b_output_centers)
    for (size_t i = 0; i < colvars.size(); i++) {
      size_t const this_cv_width = (colvars[i]->value()).output_width (cvm::cv_width);
      os << " x0_"
         << cvm::wrap_string (colvars[i]->name, this_cv_width-3);
    }

  return os;
}


std::ostream & colvarbias_alb::write_traj (std::ostream &os)
{
  os << " ";

  if (b_output_energy)
    os << " "
       << std::setprecision (cvm::en_prec) << std::setw (cvm::en_width)
       << bias_energy;

  if(b_output_coupling)
    os << " "
       << std::setprecision (cvm::en_prec) << std::setw (cvm::en_width)
       << current_coupling_force;


  if (b_output_centers)
    for (size_t i = 0; i < colvars.size(); i++) {
      os << " "
         << std::setprecision (cvm::cv_prec) << std::setw (cvm::cv_width)
         << colvar_centers[i];
    }

  if(b_output_grad) 
    for(size_t i = 0; i < means.size(); i++) {
      os << " "
	 << std::setprecision(cvm::cv_prec) << std::setw(cvm::cv_width)
	 << -(means_cu[i] - means[i] * means_sq[i] - 2. * colvar_centers[i] * means_sq[i] + 2. *
	      colvar_centers[i] * means[i] * means[i]) / cvm::boltzmann();

      os << " "
	 << std::setprecision(cvm::cv_prec) << std::setw(cvm::cv_width)
	 << (means[i].norm());

      os << " "
	 << std::setprecision(cvm::cv_prec) << std::setw(cvm::cv_width)
	 << (means_sq[i]);
      os << " "
	 << std::setprecision(cvm::cv_prec) << std::setw(cvm::cv_width)
	 << (means_cu[i].norm());



    }

  return os;
}


cvm::real colvarbias_alb::restraint_potential(cvm::real k,  colvar* x,  const colvarvalue &xcenter) const 
{
  return k * (x->value() - xcenter);
}

colvarvalue colvarbias_alb::restraint_force(cvm::real k,  colvar* x,  const colvarvalue &xcenter) const 
{
  return k * x->value();
}

cvm::real colvarbias_alb::restraint_convert_k(cvm::real k, cvm::real dist_measure) const 
{
  return k / dist_measure;
}
