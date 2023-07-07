//
// Created by csl on 7/7/23.
//

#ifndef CTRAJ_IMU_GYRO_FACTOR_HPP
#define CTRAJ_IMU_GYRO_FACTOR_HPP

#include "ctraj/factors/functor_typedef.hpp"
#include "ctraj/core/imu.h"

namespace ns_ctraj {
    template<int Order>
    struct IMUGyroFactor {
    private:
        ns_ctraj::SplineMeta<Order> _splineMeta;
        IMUFrame::Ptr _imuFrame{};

        double _dtInv;
        double _gyroWeight;

    public:
        explicit IMUGyroFactor(ns_ctraj::SplineMeta<Order> splineMeta, IMUFrame::Ptr imuFrame, double gyroWeight)
                : _splineMeta(std::move(splineMeta)), _imuFrame(std::move(imuFrame)),
                  _dtInv(1.0 / _splineMeta.segments.front().dt), _gyroWeight(gyroWeight) {}

        static auto
        Create(const ns_ctraj::SplineMeta<Order> &splineMeta, const IMUFrame::Ptr &imuFrame, double gyroWeight) {
            return new ceres::DynamicAutoDiffCostFunction<IMUGyroFactor>(
                    new IMUGyroFactor(splineMeta, imuFrame, gyroWeight)
            );
        }

        static std::size_t TypeHashCode() {
            return typeid(IMUGyroFactor).hash_code();
        }

    public:
        /**
         * param blocks:
         * [ SO3 | ... | SO3 | GYRO_BIAS | GYRO_MAP_COEFF | SO3_AtoG ]
         */
        template<class T>
        bool operator()(T const *const *sKnots, T *sResiduals) const {
            // array offset
            std::size_t SO3_OFFSET;
            double u;
            _splineMeta.template ComputeSplineIndex(_imuFrame->GetTimestamp(), SO3_OFFSET, u);

            std::size_t GYRO_BIAS_OFFSET = _splineMeta.NumParameters();
            std::size_t GYRO_MAP_COEFF_OFFSET = GYRO_BIAS_OFFSET + 1;
            std::size_t SO3_AtoG_OFFSET = GYRO_MAP_COEFF_OFFSET + 1;

            ns_ctraj::SO3Tangent<T> gyroVel;
            ns_ctraj::CeresSplineHelper<Order>::template EvaluateLie<T, Sophus::SO3>(
                    sKnots + SO3_OFFSET, u, _dtInv, nullptr, &gyroVel
            );

            Eigen::Map<const ns_ctraj::Vector3<T>> gyroBias(sKnots[GYRO_BIAS_OFFSET]);
            auto gyroCoeff = sKnots[GYRO_MAP_COEFF_OFFSET];
            ns_ctraj::Matrix3<T> gyroMapMat = ns_ctraj::Matrix3<T>::Zero();
            gyroMapMat.diagonal() = Eigen::Map<const ns_ctraj::Vector3<T>>(gyroCoeff, 3);
            gyroMapMat(0, 1) = *(gyroCoeff + 3);
            gyroMapMat(0, 2) = *(gyroCoeff + 4);
            gyroMapMat(1, 2) = *(gyroCoeff + 5);

            Eigen::Map<Sophus::SO3<T> const> const SO3_AtoG(sKnots[SO3_AtoG_OFFSET]);

            Eigen::Map<ns_ctraj::Vector3<T>> residuals(sResiduals);
            residuals = (gyroMapMat * (SO3_AtoG * gyroVel)).eval() + gyroBias - _imuFrame->GetGyro().template cast<T>();
            residuals = T(_gyroWeight) * residuals;

            return true;
        }

    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    };
}

#endif //CTRAJ_IMU_GYRO_FACTOR_HPP
