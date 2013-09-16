Used Microsoft Kinect and the Kinect SDK to develop this application.
Developed on the Skeleton Tracking sample program.
The skeleton of the human body plotted comprises of 20 joints. 
The developed code tracks the left hand and the right hand of the 
drummer. 
There are zones created for each drum part :
    1. Snare
	2. Hi hat
	3. Left Crash
	4. Ride
	5. High Tom
	6. Low Tom

All the zones were mapped relative to the shoulder joint of the skeleton.
Thus the zones were all relative to where the drummer sits. It does not 
matter if the drummer is being tracked at the edge of the screen or the 
center of the screen. All the drum parts move along with the position of 
the drummer.

The developed code also makes sure that only the downward motion of the 
drummer is being tracked and not the upward motion. This is because, in 
real life a drummer can only strike the drums in the downward motion.

The coordinates of the hand are recored along the x, y and z axes.
The z axis coordinates gives the depth of the left and right hand.
Using the depth information, the software can infer if the drummer 
is trying to play the Low Tom or High Tom as these drum parts are 
much ahead of the drummer as compared to the Snare and Hi Hat
