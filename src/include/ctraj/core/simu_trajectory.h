//
// Created by csl on 3/18/23.
//

#ifndef CTRAJ_SIMU_TRAJECTORY_H
#define CTRAJ_SIMU_TRAJECTORY_H

#include "ctraj/core/trajectory_estimator.h"
#include "ctraj/view/traj_viewer.h"

namespace ns_ctraj {
    template<int Order>
    class SimuTrajectory {
    public:
        using Ptr = std::shared_ptr<SimuTrajectory>;
        using Traj = Trajectory<Order>;
        using TrajPtr = typename Traj::Ptr;
        using TrajEstor = TrajectoryEstimator<Order>;

    protected:
        Eigen::aligned_vector<Posed> _poseSeq{};
        double _hz{};
        TrajPtr _trajectory{};

    protected:
        explicit SimuTrajectory(double sTime, double eTime, double hz)
                : _trajectory(Traj::Create(2.0 / hz, sTime, eTime)), _hz(hz) {}

        SimuTrajectory(const SimuTrajectory &other)
                : _hz(other._hz), _poseSeq(other._poseSeq), _trajectory(Traj::Create(0.0, 0.0, 0.0)) {
            *this->_trajectory = *other._trajectory;
        }

    public:
        [[nodiscard]] const Eigen::aligned_vector<Posed> &GetPoseSequence() const {
            return _poseSeq;
        }

        [[nodiscard]] const TrajPtr &GetTrajectory() const {
            return _trajectory;
        }

        [[nodiscard]] double GetPoseSequenceHz() const {
            return _hz;
        }

        void Visualization(Viewer &viewer, bool showPoseSeq = true, double trajSamplingTimeDis = 0.01) {
            if (showPoseSeq) {
                viewer.ShowPoseSequence(
                        {PoseSeqDisplay(_poseSeq, PoseSeqDisplay::Mode::ARROW),
                         PoseSeqDisplay(_trajectory->Sampling(trajSamplingTimeDis), PoseSeqDisplay::Mode::COORD)}
                );
            } else {
                viewer.ShowPoseSequence(
                        {PoseSeqDisplay(_trajectory->Sampling(trajSamplingTimeDis), PoseSeqDisplay::Mode::COORD)}
                );
            }
        }

        SimuTrajectory operator*(const Sophus::SE3d &pose) const {
            SimuTrajectory newTraj = *this;
            for (auto &item: newTraj._poseSeq) { item = Posed::FromSE3(item.se3() * pose, item.timeStamp); }
            for (int i = 0; i < newTraj._trajectory->NumKnots(); ++i) {
                auto curKnot = newTraj._trajectory->GetKnot(i);
                auto newKnot = curKnot * pose;
                newTraj._trajectory->SetKnot(newKnot, i);
            }
            return newTraj;
        }

        SimuTrajectory operator!() const {
            SimuTrajectory newTraj = *this;
            for (auto &item: newTraj._poseSeq) { item = Posed::FromSE3(item.se3().inverse(), item.timeStamp); }
            for (int i = 0; i < newTraj._trajectory->NumKnots(); ++i) {
                auto curKnot = newTraj._trajectory->GetKnot(i);
                auto newKnot = curKnot.inverse();
                newTraj._trajectory->SetKnot(newKnot, i);
            }
            return newTraj;
        }

        friend SimuTrajectory operator*(const Sophus::SE3d &pose, const SimuTrajectory &simuTrajectory) {
            SimuTrajectory newTraj = simuTrajectory;
            for (auto &item: newTraj._poseSeq) { item = Posed::FromSE3(pose * item.se3(), item.timeStamp); }
            for (int i = 0; i < newTraj._trajectory->NumKnots(); ++i) {
                auto curKnot = newTraj._trajectory->GetKnot(i);
                auto newKnot = pose * curKnot;
                newTraj._trajectory->SetKnot(newKnot, i);
            }
            return newTraj;
        }

        SimuTrajectory &operator=(const SimuTrajectory &other) {
            if (&other == this) {
                return *this;
            }
            this->_poseSeq = other._poseSeq;
            this->_hz = other._hz;
            *this->_trajectory = *other._trajectory;
            return *this;
        }

    protected:

        void SimulateTrajectory() {
            double sTime = _trajectory->MinTime(), eTime = _trajectory->MaxTime(), deltaTime = 1.0 / _hz;
            for (double t = sTime; t < eTime;) {
                _poseSeq.push_back(GenPoseSequenceAtTime(t));
                t += deltaTime;
            }
            EstimateTrajectory(_poseSeq, _trajectory);
        }

        virtual Posed GenPoseSequenceAtTime(double t) { return Posed(); }

    private:
        static void EstimateTrajectory(const Eigen::aligned_vector<Posed> &poseSeq, const TrajPtr &trajectory) {
            auto estimator = TrajEstor::Create(trajectory);

            for (const auto &item: poseSeq) {
                estimator->AddSE3Measurement(item, OptimizationOption::OPT_POS | OptimizationOption::OPT_SO3, 1.0, 1.0);
            }
            // solve
            ceres::Solver::Summary summary = estimator->Solve();
            std::cout << "estimate trajectory finished, info:" << std::endl << summary.BriefReport() << std::endl;
        }
    };

    template<int Order>
    class SimuCircularMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        double _radius;

    public:
        explicit SimuCircularMotion(double radius, double sTime = 0.0, double eTime = 2 * M_PI, double hz = 10.0)
                : _radius(radius), Parent(sTime, eTime, hz) { this->SimulateTrajectory(); }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d trans;
            trans(0) = std::cos(t) * _radius;
            trans(1) = std::sin(t) * _radius;
            trans(2) = 0.0;

            Eigen::Vector3d yAxis = -trans.normalized();
            Eigen::Vector3d xAxis = Eigen::Vector3d(-trans(1), trans(0), 0.0).normalized();
            Eigen::Vector3d zAxis = xAxis.cross(yAxis);
            Eigen::Matrix3d rotMatrix;
            rotMatrix.col(0) = xAxis;
            rotMatrix.col(1) = yAxis;
            rotMatrix.col(2) = zAxis;

            return {Sophus::SO3d(rotMatrix), trans, t};
        }
    };

    template<int Order>
    class SimuSpiralMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        double _radius;
        double _heightEachCircle;

    public:
        explicit SimuSpiralMotion(double radius, double heightEachCircle,
                                  double sTime = 0.0, double eTime = 4 * M_PI, double hz = 10.0)
                : _radius(radius), _heightEachCircle(heightEachCircle), Parent(sTime, eTime, hz) {
            this->SimulateTrajectory();
        }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d trans;
            trans(0) = std::cos(t) * _radius;
            trans(1) = std::sin(t) * _radius;
            trans(2) = t / (2.0 * M_PI) * _heightEachCircle;

            Eigen::Vector3d yAxis = -trans.normalized();
            Eigen::Vector3d xAxis = Eigen::Vector3d(-trans(1), trans(0), 0.0).normalized();
            Eigen::Vector3d zAxis = xAxis.cross(yAxis);
            Eigen::Matrix3d rotMatrix;
            rotMatrix.col(0) = xAxis;
            rotMatrix.col(1) = yAxis;
            rotMatrix.col(2) = zAxis;

            return {Sophus::SO3d(rotMatrix), trans, t};
        }
    };

    template<int Order>
    class SimuWaveMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        double _radius;
        double _height;

    public:
        explicit SimuWaveMotion(double radius, double height,
                                double sTime = 0.0, double eTime = 2 * M_PI, double hz = 10.0)
                : _radius(radius), _height(height), Parent(sTime, eTime, hz) {
            this->SimulateTrajectory();
        }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d trans;
            trans(0) = std::cos(t) * _radius;
            trans(1) = std::sin(t) * _radius;
            trans(2) = std::sin(2 * M_PI * t) * _height;

            Eigen::Vector3d yAxis = -trans.normalized();
            Eigen::Vector3d xAxis = Eigen::Vector3d(-trans(1), trans(0), 0.0).normalized();
            Eigen::Vector3d zAxis = xAxis.cross(yAxis);
            Eigen::Matrix3d rotMatrix;
            rotMatrix.col(0) = xAxis;
            rotMatrix.col(1) = yAxis;
            rotMatrix.col(2) = zAxis;

            return {Sophus::SO3d(rotMatrix), trans, t};
        }
    };

    template<int Order>
    class SimuUniformLinearMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        const Eigen::Vector3d &_from;
        const Eigen::Vector3d &_to;

    public:
        explicit SimuUniformLinearMotion(const Eigen::Vector3d &from, const Eigen::Vector3d &to,
                                         double sTime = 0.0, double eTime = 10.0, double hz = 10.0)
                : _from(from), _to(to), Parent(sTime, eTime, hz) { this->SimulateTrajectory(); }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d trans =
                    _from + (_to - _from) * t / (this->_trajectory->MaxTime() - this->_trajectory->MinTime());

            Eigen::Vector3d xAxis = (_to - _from).normalized();
            Eigen::Vector3d yAxis = Eigen::Vector3d(-xAxis(1), xAxis(0), 0.0).normalized();
            Eigen::Vector3d zAxis = xAxis.cross(yAxis);
            Eigen::Matrix3d rotMatrix;
            rotMatrix.col(0) = xAxis;
            rotMatrix.col(1) = yAxis;
            rotMatrix.col(2) = zAxis;

            return {Sophus::SO3d(rotMatrix), trans, t};
        }
    };

    template<int Order>
    class SimuUniformAcceleratedMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        const Eigen::Vector3d &_from;
        const Eigen::Vector3d &_to;

    public:
        explicit SimuUniformAcceleratedMotion(const Eigen::Vector3d &from, const Eigen::Vector3d &to,
                                              double sTime = 0.0, double eTime = 10.0, double hz = 10.0)
                : _from(from), _to(to), Parent(sTime, eTime, hz) { this->SimulateTrajectory(); }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d linearAcce =
                    (_to - _from) * 2.0 / std::pow(this->_trajectory->MaxTime() - this->_trajectory->MinTime(), 2.0);
            Eigen::Vector3d trans = _from + 0.5 * linearAcce * t * t;

            Eigen::Vector3d xAxis = (_to - _from).normalized();
            Eigen::Vector3d yAxis = Eigen::Vector3d(-xAxis(1), xAxis(0), 0.0).normalized();
            Eigen::Vector3d zAxis = xAxis.cross(yAxis);
            Eigen::Matrix3d rotMatrix;
            rotMatrix.col(0) = xAxis;
            rotMatrix.col(1) = yAxis;
            rotMatrix.col(2) = zAxis;

            return {Sophus::SO3d(rotMatrix), trans, t};
        }
    };

    template<int Order>
    class SimuDrunkardMotion : public SimuTrajectory<Order> {
    public:
        using Parent = SimuTrajectory<Order>;

    protected:
        Posed _lastState;
        std::uniform_real_distribution<double> _randStride, _randAngle;
        std::default_random_engine _engine;

    public:
        explicit SimuDrunkardMotion(const Eigen::Vector3d &origin, double maxStride, double maxAngleDeg,
                                    double sTime = 0.0, double eTime = 10.0, double hz = 10.0)
                : _lastState(Sophus::SO3d(), origin, sTime), _randStride(-maxStride, maxStride),
                  _randAngle(-maxAngleDeg / 180.0 * M_PI, maxAngleDeg / 180.0 * M_PI),
                  _engine(std::chrono::steady_clock::now().time_since_epoch().count()),
                  Parent(sTime, eTime, hz) { this->SimulateTrajectory(); }

    protected:
        Posed GenPoseSequenceAtTime(double t) override {
            Eigen::Vector3d deltaTrans = Eigen::Vector3d(_randStride(_engine), _randStride(_engine),
                                                         _randStride(_engine));

            auto rot1 = Eigen::AngleAxisd(_randAngle(_engine), Eigen::Vector3d(0.0, 0.0, 1.0));
            auto rot2 = Eigen::AngleAxisd(_randAngle(_engine), Eigen::Vector3d(0.0, 1.0, 0.0));
            auto rot3 = Eigen::AngleAxisd(_randAngle(_engine), Eigen::Vector3d(1.0, 0.0, 0.0));
            Eigen::Matrix3d deltaRotMatrix = (rot3 * rot2 * rot1).matrix();

            _lastState.timeStamp = t;
            _lastState.t = _lastState.t + deltaTrans;
            _lastState.so3 = Sophus::SO3d(deltaRotMatrix * _lastState.so3.matrix());

            return _lastState;
        }
    };

}

#endif //CTRAJ_SIMU_TRAJECTORY_H
