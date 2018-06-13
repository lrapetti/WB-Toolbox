/*
 * Copyright (C) 2018 Istituto Italiano di Tecnologia (IIT)
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the
 * GNU Lesser General Public License v2.1 or any later version.
 */

#include "SetMotorParameters.h"
#include "BlockInformation.h"
#include "Configuration.h"
#include "Log.h"
#include "Parameter.h"
#include "Parameters.h"
#include "RobotInterface.h"
#include "Signal.h"

#include <yarp/dev/ControlBoardPid.h>
#include <yarp/dev/IPidControl.h>
#include <yarp/dev/ITorqueControl.h>

#include <algorithm>
#include <iterator>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace wbt;
const std::string SetMotorParameters::ClassName = "SetMotorParameters";

// INDICES: PARAMETERS, INPUTS, OUTPUT
// ===================================

enum ParamIndex
{
    Bias = WBBlock::NumberOfParameters - 1,
    SetP,
    SetI,
    SetD,
    PidCtrlType,
    SetKTau,
    KTau,
    SetBemf,
    Bemf,
};

static int InputIndex_PGains = -1;
static int InputIndex_IGains = -1;
static int InputIndex_DGains = -1;

// BLOCK PIMPL
// ===========

class SetMotorParameters::impl
{
public:
    std::vector<yarp::dev::Pid> pidValuesDefault;
    std::vector<yarp::dev::Pid> pidValuesApplied;

    yarp::dev::PidControlTypeEnum controlType;

    bool firstRun = false;

    bool setP;
    bool setI;
    bool setD;
    bool setKTau;
    bool setBemf;

    std::vector<yarp::dev::MotorTorqueParameters> motorParamsDefault;
    std::vector<yarp::dev::MotorTorqueParameters> motorParamsApplied;
};

// BLOCK CLASS
// ===========

SetMotorParameters::SetMotorParameters()
    : pImpl{new impl()}
{}

unsigned SetMotorParameters::numberOfParameters()
{
    return WBBlock::numberOfParameters() + 8;
}

bool SetMotorParameters::parseParameters(BlockInformation* blockInfo)
{
    const std::vector<ParameterMetadata> metadata{
        {ParameterType::BOOL, ParamIndex::SetP, 1, 1, "SetP"},
        {ParameterType::BOOL, ParamIndex::SetI, 1, 1, "SetI"},
        {ParameterType::BOOL, ParamIndex::SetD, 1, 1, "SetD"},
        {ParameterType::STRING, ParamIndex::PidCtrlType, 1, 1, "ControlType"},
        {ParameterType::BOOL, ParamIndex::SetKTau, 1, 1, "SetKTau"},
        {ParameterType::BOOL, ParamIndex::SetBemf, 1, 1, "SetBemf"},
        {ParameterType::DOUBLE, ParamIndex::KTau, 1, ParameterMetadata::DynamicSize, "KTau"},
        {ParameterType::DOUBLE, ParamIndex::Bemf, 1, ParameterMetadata::DynamicSize, "Bemf"}};

    for (const auto& md : metadata) {
        if (!blockInfo->addParameterMetadata(md)) {
            wbtError << "Failed to store parameter metadata";
            return false;
        }
    }

    return blockInfo->parseParameters(m_parameters);
}

bool SetMotorParameters::configureSizeAndPorts(BlockInformation* blockInfo)
{
    if (!WBBlock::configureSizeAndPorts(blockInfo)) {
        return false;
    }

    // PARAMETERS
    // ==========

    if (!SetMotorParameters::parseParameters(blockInfo)) {
        wbtError << "Failed to parse parameters.";
        return false;
    }

    bool setP = false;
    bool setI = false;
    bool setD = false;

    bool ok = true;
    ok = ok && m_parameters.getParameter("SetP", setP);
    ok = ok && m_parameters.getParameter("SetI", setI);
    ok = ok && m_parameters.getParameter("SetD", setD);

    if (!ok) {
        wbtError << "Failed to get parameters after their parsing.";
        return false;
    }

    // INPUTS
    // ======
    //
    // 1) P: proportional gains (1xDoFs)
    // 2) I: derivative gains (1xDoFs)
    // 3) D: integral gains (1xDoFs)
    //
    // OUTPUTS
    // =======
    //
    // No outputs
    //

    int numberOfInputs = -1;
    BlockInformation::IOData ioData;

    if (setP) {
        InputIndex_PGains = ++numberOfInputs;
        ioData.input.emplace_back(
            InputIndex_PGains, std::vector<int>{Signal::DynamicSize}, DataType::DOUBLE);
    }

    if (setI) {
        InputIndex_IGains = ++numberOfInputs;
        ioData.input.emplace_back(
            InputIndex_IGains, std::vector<int>{Signal::DynamicSize}, DataType::DOUBLE);
    }

    if (setD) {
        InputIndex_DGains = ++numberOfInputs;
        ioData.input.emplace_back(
            InputIndex_DGains, std::vector<int>{Signal::DynamicSize}, DataType::DOUBLE);
    }

    if (!blockInfo->setIOPortsData(ioData)) {
        wbtError << "Failed to configure input / output ports.";
        return false;
    }

    if (!ok) {
        wbtError << "Failed to configure input / output ports.";
        return false;
    }

    return true;
}

bool SetMotorParameters::initialize(BlockInformation* blockInfo)
{
    if (!WBBlock::initialize(blockInfo)) {
        return false;
    }

    // INPUT PARAMETERS
    // ================

    if (!SetMotorParameters::parseParameters(blockInfo)) {
        wbtError << "Failed to parse parameters.";
        return false;
    }

    std::string controlType;
    std::vector<double> kTauVector;
    std::vector<double> bemfVector;

    bool ok = true;
    ok = ok && m_parameters.getParameter("SetP", pImpl->setP);
    ok = ok && m_parameters.getParameter("SetI", pImpl->setI);
    ok = ok && m_parameters.getParameter("SetD", pImpl->setD);
    ok = ok && m_parameters.getParameter("ControlType", controlType);
    ok = ok && m_parameters.getParameter("SetKTau", pImpl->setKTau);
    ok = ok && m_parameters.getParameter("SetBemf", pImpl->setBemf);
    ok = ok && m_parameters.getParameter("KTau", kTauVector);
    ok = ok && m_parameters.getParameter("Bemf", bemfVector);

    if (!ok) {
        wbtError << "Failed to get parameters after their parsing.";
        return false;
    }

    // CLASS INITIALIZATION
    // ====================

    // Get the RobotInterface, the Controlled Joints and the DoFs
    const auto robotInterface = getRobotInterface();
    const auto dofs = getRobotInterface()->getConfiguration().getNumberOfDoFs();

    if (kTauVector.size() != dofs || bemfVector.size() != dofs) {
        wbtError << "KTau and back EMF vectors don't have a width equal to " << dofs << ".";
        return false;
    }

    // Handle the P, I, D gains
    // ------------------------

    if (controlType == "Position") {
        pImpl->controlType = yarp::dev::VOCAB_PIDTYPE_POSITION;
    }
    else if (controlType == "Torque") {
        pImpl->controlType = yarp::dev::VOCAB_PIDTYPE_TORQUE;
    }
    else {
        wbtError << "Control type not recognized.";
        return false;
    }

    // Initialize the vector size to the number of dofs
    pImpl->pidValuesDefault.resize(dofs);

    // Get the interface
    yarp::dev::IPidControl* iPidControl = nullptr;
    if (!robotInterface->getInterface(iPidControl) || !iPidControl) {
        wbtError << "Failed to get IPidControl interface.";
        return false;
    }

    // Store the default gains
    if (!iPidControl->getPids(pImpl->controlType, pImpl->pidValuesDefault.data())) {
        wbtError << "Failed to get default data from IPidControl.";
        return false;
    }

    // Initialize the vector of the applied pid gains with the default gains
    pImpl->pidValuesApplied = pImpl->pidValuesDefault;

    // Handle the KTau and BEMF parameters
    // -----------------------------------

    // Get the interface
    yarp::dev::ITorqueControl* iTorqueControl = nullptr;
    if (!getRobotInterface()->getInterface(iTorqueControl) || !iTorqueControl) {
        wbtError << "Failed to get ITorqueControl interface.";
        return false;
    }

    // Resize the vectors
    pImpl->motorParamsDefault.resize(dofs);
    pImpl->motorParamsApplied.resize(dofs);

    // Get the default values
    for (unsigned m = 0; m < dofs; ++m) {
        if (!iTorqueControl->getMotorTorqueParams(m, &pImpl->motorParamsDefault[m])) {
            wbtError << "Failed to get motor torque parameters.";
            return false;
        }
    }

    // Initialize the vector of the applied parameters with the default values
    pImpl->motorParamsApplied = pImpl->motorParamsDefault;

    // Update the applied motor parameters structures with data from the mask
    for (unsigned m = 0; m < dofs; ++m) {
        if (pImpl->setKTau) {
            pImpl->motorParamsApplied[m].ktau = kTauVector[m];
        }
        if (pImpl->setBemf) {
            pImpl->motorParamsApplied[m].bemf = bemfVector[m];
        }
    }

    // VALIDATE SIGNALS SIZE
    // =====================

    if ((pImpl->setP && (blockInfo->getInputPortWidth(InputIndex_PGains) != dofs))
        || (pImpl->setI && (blockInfo->getInputPortWidth(InputIndex_IGains) != dofs))
        || (pImpl->setD && (blockInfo->getInputPortWidth(InputIndex_DGains) != dofs))) {
        wbtError << "Input ports must have a size equal to " << dofs << ".";
        return false;
    }

    return true;
}

bool SetMotorParameters::terminate(const BlockInformation* blockInfo)
{
    const auto robotInterface = getRobotInterface();
    const auto dofs = robotInterface->getConfiguration().getNumberOfDoFs();

    // Get the IPidControl interface
    yarp::dev::IPidControl* iPidControl = nullptr;
    if (!robotInterface->getInterface(iPidControl) || !iPidControl) {
        wbtError << "Failed to get IPidControl interface.";
        return false;
    }

    // Reset default PID gains
    if (!iPidControl->setPids(pImpl->controlType, pImpl->pidValuesDefault.data())) {
        wbtError << "Failed to reset PIDs to the default values.";
        return false;
    }

    // Get the ITorqueControl interface
    yarp::dev::ITorqueControl* interface = nullptr;
    if (!robotInterface->getInterface(interface) || !interface) {
        wbtError << "Failed to get ITorqueControl interface.";
        return false;
    }

    // Restore default motor torque parameters
    for (unsigned m = 0; m < dofs; ++m) {
        if (!interface->setMotorTorqueParams(m, pImpl->motorParamsDefault[m])) {
            wbtError << "Failed to restore default motor torque parameters.";
            break;
        }
    }

    return WBBlock::terminate(blockInfo);
}

bool SetMotorParameters::output(const BlockInformation* blockInfo)
{
    const auto robotInterface = getRobotInterface();
    const auto dofs = robotInterface->getConfiguration().getNumberOfDoFs();

    // At the first run apply the motor torque parameters
    if (pImpl->firstRun) {
        pImpl->firstRun = false;

        const auto& controlledJoints = robotInterface->getConfiguration().getControlledJoints();

        // Get the interface
        yarp::dev::ITorqueControl* interface = nullptr;
        if (!robotInterface->getInterface(interface) || !interface) {
            wbtError << "Failed to get ITorqueControl interface.";
            return false;
        }

        // Apply the motor parameters
        for (unsigned m = 0; m < dofs; ++m) {
            if (!interface->setMotorTorqueParams(m, pImpl->motorParamsApplied[m])) {
                wbtError << "Failed to set motor torque parameters for joint "
                         << controlledJoints[m] << ".";
                break;
            }
        }
    }

    bool ok = true;
    bool sendPids = false;

    if (pImpl->setP) {
        const Signal pGainsSignal = blockInfo->getInputPortSignal(InputIndex_PGains);
        ok = ok && pGainsSignal.isValid();

        if (!pGainsSignal.isValid()) {
            wbtError << "Failed to get signal containing proportional gains.";
        }

        for (unsigned i = 0; i < pImpl->pidValuesApplied.size(); ++i) {
            double gain = pGainsSignal.get<double>(i);
            if (pImpl->pidValuesApplied[i].kp != gain) {
                sendPids = true;
                pImpl->pidValuesApplied[i].kp = gain;
            }
        }
    }

    if (pImpl->setI) {
        const Signal iGainsSignal = blockInfo->getInputPortSignal(InputIndex_IGains);
        ok = ok && iGainsSignal.isValid();

        if (!iGainsSignal.isValid()) {
            wbtError << "Failed to get signal containing integral gains.";
        }

        for (unsigned i = 0; i < pImpl->pidValuesApplied.size(); ++i) {
            double gain = iGainsSignal.get<double>(i);
            if (pImpl->pidValuesApplied[i].ki != gain) {
                sendPids = true;
                pImpl->pidValuesApplied[i].ki = gain;
            }
        }
    }

    if (pImpl->setD) {
        const Signal dGainsSignal = blockInfo->getInputPortSignal(InputIndex_DGains);
        ok = ok && dGainsSignal.isValid();

        if (!dGainsSignal.isValid()) {
            wbtError << "Failed to get signal containing derivative gains.";
        }

        for (unsigned i = 0; i < pImpl->pidValuesApplied.size(); ++i) {
            double gain = dGainsSignal.get<double>(i);
            if (pImpl->pidValuesApplied[i].kd != gain) {
                sendPids = true;
                pImpl->pidValuesApplied[i].kd = gain;
            }
        }
    }

    if (sendPids) {
        // Get the interface
        yarp::dev::IPidControl* iPidControl = nullptr;
        if (!robotInterface->getInterface(iPidControl) || !iPidControl) {
            wbtError << "Failed to get IPidControl interface.";
            return false;
        }

        // Apply the new pid gains
        if (!iPidControl->setPids(pImpl->controlType, pImpl->pidValuesApplied.data())) {
            wbtError << "Failed to set PID values.";
            return false;
        }
    }

    return true;
}
