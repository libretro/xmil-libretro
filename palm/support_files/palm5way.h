/******************************************************************************
 *
 * Copyright (c) 1994-2002 Palm, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: PalmChars.h
 *
 * Release: 
 *
 * Description:
 *      Header file for all Palm Devices
 *      Contains Palm-specific vchrs
 *
 *****************************************************************************/

#ifndef __PALMCHARS_H__
#define __PALMCHARS_H__

#include <Chars.h>

#if (vchrPalmMin != 0x0500) || (vchrPalmMax != 0x05FF)
#error "please check Palm vchr range or update the above assertion"
#endif

#define vchrSilkClock           (vchrPalmMin + 0)
#define vchrClock               (vchrPalmMin + 1)

#define vchrPopupBrightness     (vchrPalmMin + 2)

#define vchrNavChange           (vchrPalmMin + 3)
#define vchrNavReserved0        (vchrPalmMin + 4)
#define vchrNavReserved1        (vchrPalmMin + 5)
#define vchrNavReserved2        (vchrPalmMin + 6)
#define vchrNavReserved3        (vchrPalmMin + 7)

#define keyBitNavLeft           0x01000000
#define keyBitNavRight          0x02000000
#define keyBitNavSelect         0x04000000
#define keyBitNavLRS            0x07000000

#define navBitUp                0x0001
#define navBitDown              0x0002
#define navBitLeft              0x0004
#define navBitRight             0x0008
#define navBitSelect            0x0010
#define navBitsAll              0x001F

#define navChangeUp             0x0100
#define navChangeDown           0x0200
#define navChangeLeft           0x0400
#define navChangeRight          0x0800
#define navChangeSelect         0x1000
#define navChangeBitsAll        0x1F00

#define navFtrCreator           'fway'
#define navFtrVersion           0

#define navVersion              0x00010000      /* header version 1.0 */


// A macro to use for apps that support navigation using the 5-way.
// This macro will let you test whether an event was generated by the 5-way.
// For applications that treat up and down hard buttons differently than up and
// down on the 5-way, this macro can be used to determine which action to take
// if NavKeyPressed returns true for Up or Down.
//
// Usage:	if (IsFiveWayNavEvent(eventP))
//
#define IsFiveWayNavEvent(eventP)																		\
	(																												\
		 ((eventP)->data.keyDown.chr == vchrPageUp ||												\
		  (eventP)->data.keyDown.chr == vchrPageDown ||												\
		  (eventP)->data.keyDown.chr == vchrNavChange)												\
	&&																												\
		 (((eventP)->data.keyDown.keyCode & (navBitsAll | navChangeBitsAll)) != 0)			\
	)


// A macro to use for apps that support navigation using the 5-way.
// By using this macro, we have consistent behavior in all our apps
// when it comes to how it handles multiple simultaneous button presses.
//
// Usage:	if (NavSelectPressed(eventP))
//
// Only act when the select button is released, and only if none of the
// direction buttons are down (or being released) at the time, and only if the select
// button has not been held down long enough to start repeating. By ignoring repeat
// events for the select button, we ensure that the launcher will be run if the select
// button is held down. By waiting until the button is released, we ensure that the
// select action is not taken before the launcher is run.
#define NavSelectPressed(eventP)																			\
	(																												\
		IsFiveWayNavEvent(eventP)																			\
	&&																												\
		(((eventP)->data.keyDown.modifiers & autoRepeatKeyMask) == 0)							\
	&&																												\
		(((eventP)->data.keyDown.keyCode & (navBitsAll | navChangeBitsAll)) ==				\
																		 navChangeSelect)						\
	)


// A macro to use for apps that support navigation using the 5-way.
// By using this macro, we have consistent behavior in all our apps
// when it comes to how it handles multiple simultaneous button presses.
// You can use this macro even if the device does not have a 5-way controller.
//
// Usage:	if (NavDirectionPressed(eventP, Left))
// 			if (NavDirectionPressed(eventP, Right))
// 			if (NavDirectionPressed(eventP, Up))			- also works without 5-way
// 			if (NavDirectionPressed(eventP, Down))			- also works without 5-way
//
// Act only when one direction is pressed without any other direction (or select) being
// down at the time. Repeat if the button is held down, but not if other buttons are
// pressed.
#define NavDirectionPressed(eventP, nav)																		\
	(IsFiveWayNavEvent(eventP)																				\
		? (((eventP)->data.keyDown.modifiers & autoRepeatKeyMask)								\
		 	? (((eventP)->data.keyDown.keyCode & (navBitsAll | navChangeBitsAll)) ==		\
		 				(navBit ## nav))																		\
		 	: (((eventP)->data.keyDown.keyCode & (navBitsAll | navChangeBitsAll)) ==		\
						(navBit ## nav | navChange ## nav)))											\
		: (((eventP)->data.keyDown.chr == vchrPageUp && navBit ## nav == navBitUp) ||		\
		 	((eventP)->data.keyDown.chr == vchrPageDown && navBit ## nav == navBitDown))	\
	)


// A macro to use for apps that support navigation using the 5-way.
// By using this macro, we have consistent behavior in all our apps
// when it comes to how it handles multiple simultaneous button presses.
// You can use this macro even if the device does not have a 5-way controller.
//
// Usage:	if (NavKeyPressed(eventP, Select))
// 			if (NavKeyPressed(eventP, Left))
// 			if (NavKeyPressed(eventP, Right))
// 			if (NavKeyPressed(eventP, Up))			- also works without 5-way
// 			if (NavKeyPressed(eventP, Down))			- also works without 5-way
//
#define NavKeyPressed(eventP, nav)																		\
	((navBit ## nav == navBitSelect)																		\
		? NavSelectPressed(eventP)																			\
		: NavDirectionPressed(eventP, nav)																\
	)

#endif /* __PALMCHARS_H__ */
