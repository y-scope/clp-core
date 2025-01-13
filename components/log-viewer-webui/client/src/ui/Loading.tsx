import React from "react";

import {
    Box,
    LinearProgress,
    Sheet,
    Step,
    StepIndicator,
    Stepper,
    Typography,
} from "@mui/joy";
import {DefaultColorPalette} from "@mui/joy/styles/types";

import {Nullable} from "../typings/common";
import {
    QUERY_LOADING_STATE,
    QUERY_LOADING_STATE_DESCRIPTIONS,
    QUERY_LOADING_STATE_VALUES,
} from "../typings/query";

import "./Loading.css";


interface LoadingStepProps {
    description: string;
    isActive: boolean;
    isError: boolean;
    label: string;
    stepIndicatorText: number | string;
}

/**
 * Renders a step with a label and description.
 *
 * @param props
 * @param props.description
 * @param props.isActive
 * @param props.isError
 * @param props.label
 * @param props.stepIndicatorText
 * @return
 */
const LoadingStep = ({
    description,
    isActive,
    isError,
    label,
    stepIndicatorText,
}: LoadingStepProps) => {
    let color: DefaultColorPalette = isActive ?
        "primary" :
        "neutral";

    if (isError) {
        color = "danger";
    }

    return (
        <Step
            indicator={
                <StepIndicator
                    color={color}
                    variant={isActive ?
                        "solid" :
                        "outlined"}
                >
                    {stepIndicatorText}
                </StepIndicator>
            }
        >
            <Typography
                color={color}
                level={"title-lg"}
            >
                {label}
            </Typography>
            <Typography level={"body-sm"}>
                {description}
            </Typography>
        </Step>
    );
};

interface LoadingProps {
    currentState: QUERY_LOADING_STATE;
    errorMsg: Nullable<string>;
}

/**
 * Displays status of a pending query job.
 *
 * @param props
 * @param props.currentState
 * @param props.errorMsg
 * @return
 */
const Loading = ({
    currentState,
    errorMsg,
}:LoadingProps) => {
    const steps: React.ReactNode[] = [];
    QUERY_LOADING_STATE_VALUES.forEach((state) => {
        const isActive = (currentState === state);
        const stateDescription = QUERY_LOADING_STATE_DESCRIPTIONS[state];
        steps.push(
            <LoadingStep
                description={stateDescription.description}
                isActive={isActive}
                isError={false}
                key={state}
                label={stateDescription.label}
                stepIndicatorText={state + 1}/>
        );
        if (isActive && null !== errorMsg) {
            steps.push(
                <LoadingStep
                    description={errorMsg}
                    isActive={isActive}
                    isError={true}
                    key={`${state}-error`}
                    label={"Error"}
                    stepIndicatorText={"X"}/>
            );
        }
    });

    return (
        <Sheet className={"loading-sheet"}>
            <Box className={"loading-progress-container"}>
                <LinearProgress
                    determinate={null !== errorMsg}
                    color={null === errorMsg ?
                        "primary" :
                        "danger"}/>
            </Box>
            <Box className={"loading-stepper-container"}>
                <Stepper
                    className={"loading-stepper"}
                    orientation={"vertical"}
                    size={"lg"}
                >
                    {steps}
                </Stepper>
            </Box>
        </Sheet>
    );
};

export default Loading;
