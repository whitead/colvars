#include "colvarmodule.h"
#include "colvarbias_alb.h"
#include "colvarbias.h"

colvarbias_alb::colvarbias_alb(std::string const &conf, char const *key) :
  colvarbias(conf, key), coupling_force(0.0), update_calls(0) {

  // get the initial restraint centers
  colvar_centers.resize (colvars.size());
  
  means.resize(colvars.size());
  means_sq.resize(colvars.size());

  for (size_t i = 0; i < colvars.size(); i++) {
    colvar_centers[i].type (colvars[i]->type());

    //copy colvars for mean
    means[i].type (colvars[i]->type());
    means[i] = colvarvalue(colvars[i]->value());
    //set them to null
    means[i].reset();
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

  get_keyval (conf, "outputCenters", b_output_centers, false);
  get_keyval (conf, "outputVariance", b_output_var, false);
  get_keyval (conf, "outputCoupling", b_output_coupling, true);

  if (cvm::debug())
    cvm::log ("Done initializing a new adaptive linear bias.\n");

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
    colvar_forces[i] = -restraint_force(restraint_convert_k(coupling_force, colvars[i]->width),
					colvars[i],
					colvar_centers[i]);
    bias_energy += restraint_potential(restraint_convert_k(coupling_force, colvars[i]->width),
				       colvars[i],
				       colvar_centers[i]);
    //scale down without copying
    means[i] *= (update_calls - 1.) / update_calls;
    means_sq[i] *= (update_calls - 1.) / update_calls;

    //add with copy from divide :(
    means[i] += colvars[i]->value() / static_cast<cvm::real> (update_calls);

    means_sq[i] += colvars[i]->value().norm2() / static_cast<cvm::real> (update_calls);
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

  if (!get_keyval (conf, "couplingForce", coupling_force))
    cvm::fatal_error ("Error: coupling force is missing from the restart.\n");

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
     << std::setw (cvm::en_width) << coupling_force << "\n";


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

  if(b_output_var)
    for(size_t i = 0; i < means.size(); i++) {
      os << "Var_"
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
       << coupling_force;


  if (b_output_centers)
    for (size_t i = 0; i < colvars.size(); i++) {
      os << " "
         << std::setprecision (cvm::cv_prec) << std::setw (cvm::cv_width)
         << colvar_centers[i];
    }

  if(b_output_var) 
    for(size_t i = 0; i < means.size(); i++) {
      os << " "
	 << std::setprecision(cvm::cv_prec) << std::setw(cvm::cv_width)
	<< means_sq[i] - means[i].norm2();

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
