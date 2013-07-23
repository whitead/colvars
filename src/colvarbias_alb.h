#ifndef COLVARBIAS_ALB_H
#define COLVARBIAS_ALB_H

#include "colvar.h"
#include "colvarbias.h"

class colvarbias_alb : public colvarbias {
  
public:
  colvarbias_alb(std::string const &conf, char const *key);
  virtual ~colvarbias_alb();
  
  cvm::real update();
protected:

  /// \brief Restraint centers
  std::vector<colvarvalue> colvar_centers;  

  /// \brief colvar moments, used for calculating variance
  std::vector<colvarvalue> means;
  std::vector<cvm::real> means_sq;
  unsigned long int update_calls;

  /// \brief current coupling force
  cvm::real coupling_force; 

  /// \brief flag for outputting colvar centers
  bool b_output_centers;

  /// \brief flag for outputting colvar estimated fluctuations
  bool b_output_var;

  /// \brief flag for outputting coupling force
  bool b_output_coupling;

  cvm::real restraint_potential(cvm::real k,  colvar*  x, const colvarvalue& xcenter) const;

  /// \brief Force function
  colvarvalue restraint_force(cvm::real k,  colvar* x,  const colvarvalue& xcenter) const;

  ///\brief Unit scaling
  cvm::real restraint_convert_k(cvm::real k, cvm::real dist_measure) const;

};

#endif
