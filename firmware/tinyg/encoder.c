/*
 * encoder.c - encoder interface
 * This file is part of the TinyG project
 *
 * Copyright (c) 2013 Alden S. Hart, Jr.
 *
 * This file ("the software") is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by the
 * Free Software Foundation. You should have received a copy of the GNU General Public
 * License, version 2 along with the software.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, you may use this file as part of a software library without
 * restriction. Specifically, if other files instantiate templates or use macros or
 * inline functions from this file, or you compile this file and link it with  other
 * files to produce an executable, this file does not by itself cause the resulting
 * executable to be covered by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be covered by the
 * GNU General Public License.
 *
 * THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "tinyg.h"
#include "config.h"
#include "planner.h"
#include "stepper.h"
#include "encoder.h"
#include "kinematics.h"
#include "canonical_machine.h"
#include "hardware.h"

/**** Allocate Structures ****/

enEncoders_t en;

/************************************************************************************
 **** CODE **************************************************************************
 ************************************************************************************/

/* 
 * encoder_init() - initialize encoders 
 */

void encoder_init()
{
	memset(&en, 0, sizeof(en));		// clear all values, pointers and status
	en.magic_end = MAGICNUM;
	en.magic_start = MAGICNUM;
}

/*
 * en_assertions() - test assertions, return error code if violation exists
 */

stat_t en_assertions()
{
	if (en.magic_end   != MAGICNUM) return (STAT_STEPPER_ASSERTION_FAILURE);
	if (en.magic_start != MAGICNUM) return (STAT_STEPPER_ASSERTION_FAILURE);
	return (STAT_OK);
}

/* 
 * en_reset_encoders() - initialize encoder values and position
 *
 *	en_reset_encoder() reset the encoders at the start of a machining cycle.
 *	This sets the position and target and zeros all step counts. Position and target
 *	are delivered as floats in axis space (work space). These need to be converted to
 *	integer steps in motor space (joint apsce). This establishes the "step grid" 
 *	relative to the current machine position.
 *
 *	Reset is called on cycle start which can have the following cases:
 *
 *	  -	New cycle from G0. Position and target from Gcode model (MODEL). (canonical_machine, cm_straight_traverse()
 *	  -	New cycle from G1. Position and target from Gcode model (MODEL). (canonical_machine, cm_straight_feed()
 *	  -	New cycle from G2/G3. Position &target from Gcode model (MODEL). (plan_arc.c,  cm_arc_feed()
 *
 *	The above is also true of cycle starts called from within homing, probing, jogging or other canned cycles.
 *
 *	  - Cycle (re)start from feedhold. Position and target from runtime exec (RUNTIME);
 *		(canonical_machine.c, cm_request_cycle_start() )		
 *
 *	  - mp_exec_move() can also perform a cycle start, but wouldn't this always be 
 *		started by the calling G0/G1/G2/G3? Test this (planner.c, ln 175)
 */

//void en_reset_encoders(GCodeState_t *model)
void en_reset_encoders(void)
{
//	GCodeState_t *model = MODEL;

	// get position and target and transform to joint space as floats
//	if (model == MODEL) {
		ik_kinematics(cm.gm.target, en.target_steps_next);
		ik_kinematics(cm.gmx.position, en.position_steps);
//		ik_kinematics(model->target, en.target_steps_next);
//	} else {	// get it from the runtime
//		ik_kinematics(mr.position, en.position_steps);
//		ik_kinematics(mr.target, en.target_steps_next);
//	}

//	mp_get_runtime_target_steps(en.target_steps_next);	// read initial target
//	void mp_get_runtime_target_steps(float target_steps[]) 
//	{ 
//		ik_kinematics(mr.target, target_steps);
//	} // transform to joint space

	for (uint8_t i=0; i<MOTORS; i++) {
		en.en[i].target_steps = (int32_t)round(en.target_steps_next[i]);// transfer initial target to working target
		en.en[i].position_steps = (int32_t)round(en.position_steps[i]);
//		en.en[i].position_steps = cm.gmx.position[i] * st_cfg.mot[i].steps_per_unit;
		en.en[i].position_steps_advisory = en.en[i].position_steps;		// initial approximation
	}
}

/* 
 * en_sample_position_error()
 *
 *	en_sample_position_error() should be called by PREP whenever the last_segment 
 *	flag is set (by the stepper ISR). The position results will be stable for the duration
 *	of the segment (5ms) immediately following the last_segment flag. It does a few things:
 *
 *	- It loads the target currently in the EXEC runtime, which is one move ahead of the 
 *	  move that was just counted. This saves the target so it can be used later for 
 *	  computing the error term for the next move.
 *
 *  - It computes the position error in steps and in MM. The MM ppsition is advisory only
 *	  as it relates to the Axis (not the Motor) and assumes a cartesian machine. Error correction
 *	  should always be performed using position_error_steps, not the position_error_float.
 *
 *	  The error term remains stable until the next time en_sample_position_error() is called
 */

void en_sample_position_error()
{
//	if (en.last_segment == false) return;	// Interlock. Should not run if flag is false.

	mp_get_runtime_target_steps(en.target_steps_next);

	for (uint8_t i=0; i<MOTORS; i++) {
		en.en[i].position_error_steps = en.en[i].position_steps - en.en[i].target_steps;
		en.en[i].position_error_advisory = (float)en.en[i].position_error_steps * st_cfg.mot[i].units_per_step;
		en.en[i].target_steps = (int32_t)round(en.target_steps_next[i]);// transfer staged target to working target
	}

//	printf("{\"en%d\":{\"steps_flt\":%0.3f,\"pos_st\":%li,\"tgt_st\":%li,\"err_st\":%li,\"err_d\":%0.5f}}\n",
//		MOTOR_2+1,
//		(double)en.en[MOTOR_2].position_steps_advisory,
//		en.en[MOTOR_2].position_steps, 
//		en.en[MOTOR_2].target_steps,
//		en.en[MOTOR_2].position_error_steps,
//		(double)en.en[MOTOR_2].position_error_advisory);

//	printf("{\"en%d\":{\"steps_flt\":%0.3f,\"pos_st\":%li,\"tgt_st\":%li,\"err_st\":%li,\"err_d\":%0.5f}}\n\n",
//		MOTOR_3+1,
//		(double)en.en[MOTOR_3].position_steps_advisory,
//		en.en[MOTOR_3].position_steps, 
//		en.en[MOTOR_3].target_steps,
//		en.en[MOTOR_3].position_error_steps,
//		(double)en.en[MOTOR_3].position_error_advisory);

}

/*
 * DIAGNOSTICS
 * en_update_position_steps_advisory() - add new incoming steps. Handy diagnostic. It's not used for anything else.
 * en_print_encoder()
 * en_print_encoders()
 */

void en_update_position_steps_advisory(const float steps[])
{
	for (uint8_t i=0; i<MOTORS; i++) {
		en.en[i].position_steps_advisory += steps[i];
	}
}

void en_print_encoder(const uint8_t motor)
{
//	en_sample_position_error();

	printf("{\"en%d\":{\"steps_flt\":%0.3f,\"pos_st\":%li,\"tgt_st\":%li,\"err_st\":%li,\"err_d\":%0.5f}}\n",
		motor+1,
		(double)en.en[motor].position_steps_advisory,
		en.en[motor].position_steps, 
		en.en[motor].target_steps,
		en.en[motor].position_error_steps,
		(double)en.en[motor].position_error_advisory);
}

void en_print_encoders()
{
	en_sample_position_error();

	for (uint8_t i=0; i<MOTORS; i++) {
		printf("{\"en%d\":{\"steps_flt\":%0.3f,\"pos_st\":%li,\"tgt_st\":%li,\"err_st\":%li,\"err_d\":%0.5f}}\n",
			i+1,
			(double)en.en[i].position_steps_advisory,
			en.en[i].position_steps, 
			en.en[i].target_steps,
			en.en[i].position_error_steps,
			(double)en.en[i].position_error_advisory);
	}
}

/***********************************************************************************
 * CONFIGURATION AND INTERFACE FUNCTIONS
 * Functions to get and set variables from the cfgArray table
 ***********************************************************************************/

/***********************************************************************************
 * TEXT MODE SUPPORT
 * Functions to print variables from the cfgArray table
 ***********************************************************************************/

#ifdef __TEXT_MODE

#endif // __TEXT_MODE
