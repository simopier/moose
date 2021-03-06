/****************************************************************/
/* MOOSE - Multiphysics Object Oriented Simulation Environment  */
/*                                                              */
/*          All contents are licensed under LGPL V2.1           */
/*             See LICENSE for full restrictions                */
/****************************************************************/

#include "PorousFlowMassVolumetricExpansion.h"

template<>
InputParameters validParams<PorousFlowMassVolumetricExpansion>()
{
  InputParameters params = validParams<TimeKernel>();
  params.addParam<unsigned int>("fluid_component", 0, "The index corresponding to the component for this kernel");
  params.addRequiredParam<UserObjectName>("PorousFlowDictator", "The UserObject that holds the list of Porous-Flow variable names.");
  params.addClassDescription("Component_mass*rate_of_solid_volumetric_expansion");
  return params;
}

PorousFlowMassVolumetricExpansion::PorousFlowMassVolumetricExpansion(const InputParameters & parameters) :
    TimeKernel(parameters),
    _fluid_component(getParam<unsigned int>("fluid_component")),
    _dictator(getUserObject<PorousFlowDictator>("PorousFlowDictator")),
    _var_is_porflow_var(!_dictator.notPorousFlowVariable(_var.number())),
    _porosity(getMaterialProperty<Real>("PorousFlow_porosity_nodal")),
    _dporosity_dvar(getMaterialProperty<std::vector<Real> >("dPorousFlow_porosity_nodal_dvar")),
    _dporosity_dgradvar(getMaterialProperty<std::vector<RealGradient> >("dPorousFlow_porosity_nodal_dgradvar")),
    _fluid_density(getMaterialProperty<std::vector<Real> >("PorousFlow_fluid_phase_density")),
    _dfluid_density_dvar(getMaterialProperty<std::vector<std::vector<Real> > >("dPorousFlow_fluid_phase_density_dvar")),
    _fluid_saturation(getMaterialProperty<std::vector<Real> >("PorousFlow_saturation_nodal")),
    _dfluid_saturation_dvar(getMaterialProperty<std::vector<std::vector<Real> > >("dPorousFlow_saturation_nodal_dvar")),
    _mass_frac(getMaterialProperty<std::vector<std::vector<Real> > >("PorousFlow_mass_frac")),
    _dmass_frac_dvar(getMaterialProperty<std::vector<std::vector<std::vector<Real> > > >("dPorousFlow_mass_frac_dvar")),
    _strain_rate_qp(getMaterialProperty<Real>("PorousFlow_volumetric_strain_rate_qp")),
    _dstrain_rate_qp_dvar(getMaterialProperty<std::vector<RealGradient> >("dPorousFlow_volumetric_strain_rate_qp_dvar"))
{
  if (_fluid_component >= _dictator.numComponents())
    mooseError("The Dictator proclaims that the number of components in this simulation is " << _dictator.numComponents() << " whereas you have used the Kernel PorousFlowComponetMassVolumetricExpansion with component = " << _fluid_component << ".  The Dictator is watching you");
}

Real
PorousFlowMassVolumetricExpansion::computeQpResidual()
{
  mooseAssert(_fluid_component < _mass_frac[_i][0].size(), "PorousFlowMassVolumetricExpansion: fluid_component is given as " << _fluid_component << " which must be less than the number of fluid components described by the mass-fraction matrix, which is " << _mass_frac[_i][0].size());
  unsigned int num_phases = _fluid_density[_i].size();
  mooseAssert(num_phases == _fluid_saturation[_i].size(), "PorousFlowMassVolumetricExpansion: Size of fluid density = " << num_phases << " size of fluid saturation = " << _fluid_saturation[_i].size() << " but both these must be equal to the number of phases in the system");

  Real mass = 0.0;
  for (unsigned ph = 0; ph < num_phases; ++ph)
    mass += _fluid_density[_i][ph] * _fluid_saturation[_i][ph] * _mass_frac[_i][ph][_fluid_component];

  return _test[_i][_qp] * mass * _porosity[_i] * _strain_rate_qp[_qp];
}

Real
PorousFlowMassVolumetricExpansion::computeQpJacobian()
{
  return computedMassQpJac(_var.number()) + computedVolQpJac(_var.number());
}

Real
PorousFlowMassVolumetricExpansion::computeQpOffDiagJacobian(unsigned int jvar)
{
  return computedMassQpJac(jvar) + computedVolQpJac(jvar);
}

Real
PorousFlowMassVolumetricExpansion::computedVolQpJac(unsigned int jvar)
{
  if (_dictator.notPorousFlowVariable(jvar))
    return 0.0;

  const unsigned int pvar = _dictator.porousFlowVariableNum(jvar);

  unsigned int num_phases = _fluid_density[_i].size();
  Real mass = 0.0;
  for (unsigned ph = 0; ph < num_phases; ++ph)
    mass += _fluid_density[_i][ph] * _fluid_saturation[_i][ph] * _mass_frac[_i][ph][_fluid_component];

  Real dvol = _dstrain_rate_qp_dvar[_qp][pvar] * _grad_phi[_j][_qp];

  return _test[_i][_qp] * mass * _porosity[_i] * dvol;
}
Real
PorousFlowMassVolumetricExpansion::computedMassQpJac(unsigned int jvar)
{
  if (_dictator.notPorousFlowVariable(jvar))
    return 0.0;

  const unsigned int pvar = _dictator.porousFlowVariableNum(jvar);

  const unsigned int num_phases = _fluid_density[_i].size();
  Real dmass = 0.0;
  for (unsigned ph = 0; ph < num_phases; ++ph)
    dmass += _fluid_density[_i][ph] * _fluid_saturation[_i][ph] * _mass_frac[_i][ph][_fluid_component] * _dporosity_dgradvar[_i][pvar] * _grad_phi[_j][_i];

  if (_i != _j)
    return _test[_i][_qp] * dmass * _strain_rate_qp[_qp];

  for (unsigned ph = 0; ph < num_phases; ++ph)
  {
    dmass += _dfluid_density_dvar[_i][ph][pvar] * _fluid_saturation[_i][ph] * _mass_frac[_i][ph][_fluid_component] * _porosity[_i];
    dmass += _fluid_density[_i][ph] * _dfluid_saturation_dvar[_i][ph][pvar] * _mass_frac[_i][ph][_fluid_component] * _porosity[_i];
    dmass += _fluid_density[_i][ph] * _fluid_saturation[_i][ph] * _dmass_frac_dvar[_i][ph][_fluid_component][pvar] * _porosity[_i];
    dmass += _fluid_density[_i][ph] * _fluid_saturation[_i][ph] * _mass_frac[_i][ph][_fluid_component] * _dporosity_dvar[_i][pvar];
  }

  return _test[_i][_qp] * dmass * _strain_rate_qp[_qp];
}
