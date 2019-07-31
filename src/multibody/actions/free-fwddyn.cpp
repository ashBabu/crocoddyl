#include "crocoddyl/multibody/actions/free-fwddyn.hpp"
#include <pinocchio/algorithm/aba.hpp>
#include <pinocchio/algorithm/aba-derivatives.hpp>
#include <pinocchio/algorithm/rnea-derivatives.hpp>
#include <pinocchio/algorithm/compute-all-terms.hpp>
#include <pinocchio/algorithm/kinematics.hpp>
#include <pinocchio/algorithm/jacobian.hpp>
#include <pinocchio/algorithm/frames.hpp>

namespace crocoddyl {

DifferentialActionModelFreeFwdDynamics::DifferentialActionModelFreeFwdDynamics(StateMultibody* const state,
                                                                               CostModelSum* const costs)
    : DifferentialActionModelAbstract(state, state->get_model()->nv, costs->get_nr()),
      costs_(costs),
      pinocchio_(state->get_model()),
      force_aba_(true) {}

DifferentialActionModelFreeFwdDynamics::~DifferentialActionModelFreeFwdDynamics() {}

void DifferentialActionModelFreeFwdDynamics::calc(boost::shared_ptr<DifferentialActionDataAbstract>& data,
                                                  const Eigen::Ref<const Eigen::VectorXd>& x,
                                                  const Eigen::Ref<const Eigen::VectorXd>& u) {
  DifferentialActionDataFreeFwdDynamics* d = static_cast<DifferentialActionDataFreeFwdDynamics*>(data.get());
  const Eigen::VectorXd& q = x.head(nq_);
  const Eigen::VectorXd& v = x.tail(nv_);

  // Computing the dynamics using ABA or manually for armature case
  if (force_aba_) {
    d->xout = pinocchio::aba(*pinocchio_, d->pinocchio, q, v, u);
  } else {
    pinocchio::computeAllTerms(*pinocchio_, d->pinocchio, q, v);
    d->M = d->pinocchio.M;
    d->M.diagonal() += armature_;
    d->pinocchio.Minv = d->M.inverse();
    d->xout = d->pinocchio.Minv * (u - d->pinocchio.nle);
  }

  // Computing the cost value and residuals
  pinocchio::forwardKinematics(*pinocchio_, d->pinocchio, q, v);
  pinocchio::updateFramePlacements(*pinocchio_, d->pinocchio);
  costs_->calc(d->costs, x, u);
  d->cost = d->costs->cost;
}

void DifferentialActionModelFreeFwdDynamics::calcDiff(boost::shared_ptr<DifferentialActionDataAbstract>& data,
                                                      const Eigen::Ref<const Eigen::VectorXd>& x,
                                                      const Eigen::Ref<const Eigen::VectorXd>& u, const bool& recalc) {
  if (recalc) {
    calc(data, x, u);
  }
  DifferentialActionDataFreeFwdDynamics* d = static_cast<DifferentialActionDataFreeFwdDynamics*>(data.get());
  const Eigen::VectorXd& q = x.head(nq_);
  const Eigen::VectorXd& v = x.tail(nv_);

  // Computing the dynamics derivatives
  if (force_aba_) {
    pinocchio::computeABADerivatives(*pinocchio_, d->pinocchio, q, v, u);
    d->Fx.leftCols(nv_) = d->pinocchio.ddq_dq;
    d->Fx.rightCols(nv_) = d->pinocchio.ddq_dv;
    d->Fu = d->pinocchio.Minv;
  } else {
    pinocchio::computeRNEADerivatives(*pinocchio_, d->pinocchio, q, v, d->xout);
    d->Fx.leftCols(nv_) = -d->pinocchio.Minv * d->pinocchio.dtau_dq;
    d->Fx.rightCols(nv_) = -d->pinocchio.Minv * d->pinocchio.dtau_dv;
    d->Fu = d->pinocchio.Minv;
  }

  // Computing the cost derivatives
  pinocchio::computeJointJacobians(*pinocchio_, d->pinocchio, q);
  // pinocchio::updateFramePlacements(*pinocchio_, d->pinocchio); //TODO why? we run it in calc()
  costs_->calcDiff(d->costs, x, u, false);
}

boost::shared_ptr<DifferentialActionDataAbstract> DifferentialActionModelFreeFwdDynamics::createData() {
  return boost::make_shared<DifferentialActionDataFreeFwdDynamics>(this);
}

void DifferentialActionModelFreeFwdDynamics::setArmature(const Eigen::VectorXd& armature) {
  if (armature.size() != nv_) {
    std::cout << "The armature dimension is wrong, we cannot set it." << std::endl;
  } else {
    armature_ = armature;
    force_aba_ = false;
  }
}

CostModelSum* DifferentialActionModelFreeFwdDynamics::get_costs() const { return costs_; }

pinocchio::Model* DifferentialActionModelFreeFwdDynamics::get_pinocchio() const { return pinocchio_; }

}  // namespace crocoddyl