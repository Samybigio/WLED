/*
  FXparticleSystem.cpp

  Particle system with functions for particle generation, particle movement and particle rendering to RGB matrix.
  by DedeHai (Damian Schneider) 2013-2024

  LICENSE
  The MIT License (MIT)
  Copyright (c) 2024  Damian Schneider
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

/*
	Note on ESP32: using 32bit integer is faster than 16bit or 8bit, each operation takes on less instruction, can be testen on https://godbolt.org/
	it does not matter if using int, unsigned int, uint32_t or int32_t, the compiler will make int into 32bit
	this should be used to optimize speed but not if memory is affected much
*/

/*
  TODO:
  -init funktion für sprays: alles auf null setzen, dann muss man im FX nur noch setzten was man braucht
  -pass all pointers by reference to make it consistene throughout the code (or not?)
  -add local buffer for faster rendering (-> it is allowed to do so) -> run a test, it crashes. need to find out why exatly
  -add possiblity to emit more than one particle, just pass a source and the amount to emit or even add several sources and the amount, function decides if it should do it fair or not
  -add an x/y struct, do particle rendering using that, much easier to read
  -extend rendering to more than 2x2, 3x2 (fire) should be easy, 3x3 maybe also doable without using much math (need to see if it looks good)
   -das system udpate kann fire nicht handlen, es braucht auch noch ein fire update. die funktion kann einen parameter nehmen mit 'use palette'
  //todo: eine funktion für init fire? dann wäre der FX etwas aufgeräumter...
  -need a random emit? one that does not need an emitter but just takes some properties, so FX can implement their own emitters?
  -line emit wäre noch was, der die PS source anders interpretiert

*/
// sources need to be updatable by the FX, so functions are needed to apply it to a single particle that are public
#include "FXparticleSystem.h"
#include "wled.h"
#include "FastLED.h"
#include "FX.h"

ParticleSystem::ParticleSystem(uint16_t width, uint16_t height, uint16_t numberofparticles, uint16_t numberofsources)
{
	Serial.print("initializing PS... ");

	numParticles = numberofparticles; // set number of particles in the array
	usedParticles = numberofparticles; // use all particles by default
	particlesettings = {false, false, false, false, false, false, false, false}; // all settings off by default
	setPSpointers(numberofsources);	   // set the particle and sources pointer (call this before accessing sprays or particles)
	setMatrixSize(width, height);
	setWallHardness(255); // set default wall hardness to max
	emitIndex = 0;
	for (int i = 0; i < numParticles; i++)
	{
		particles[i].ttl = 0; //initialize all particles to dead
	}
	Serial.println("PS Constructor done");
}

//update function applies gravity, moves the particles, handles collisions and renders the particles
void ParticleSystem::update(void)
{
	uint32_t i;
	//apply gravity globally if enabled
	if (particlesettings.useGravity)
		applyGravity(particles, usedParticles, gforce, &gforcecounter);
	
	//move all particles
	for (i = 0; i < usedParticles; i++)
	{
		ParticleMoveUpdate(particles[i], particlesettings);
	}

	//handle collisions after moving the particles
	if (particlesettings.useCollisions)
		handleCollisions();

	//render the particles
	ParticleSys_render();
}

//update function for fire animation
void ParticleSystem::updateFire(uint8_t colormode)
{
	
	// update all fire particles
	FireParticle_update();
	
	// render the particles
	renderParticleFire(colormode);
}

void ParticleSystem::setUsedParticles(uint32_t num)
{
	usedParticles = min(num, numParticles); //limit to max particles
}

void ParticleSystem::setWallHardness(uint8_t hardness)
{
	wallHardness = hardness + 1; // at a value of 256, no energy is lost in collisions
}

void ParticleSystem::setCollisionHardness(uint8_t hardness)
{
	collisionHardness = hardness + 1; // at a value of 256, no energy is lost in collisions
}

void ParticleSystem::setMatrixSize(uint16_t x, uint16_t y)
{
	maxXpixel = x - 1; // last physical pixel that can be drawn to
	maxYpixel = y - 1;
	maxX = x * PS_P_RADIUS + PS_P_HALFRADIUS - 1; // particle system boundaries, allow them to exist one pixel out of boundaries for smooth leaving/entering when kill out of bounds is set
	maxY = y * PS_P_RADIUS + PS_P_HALFRADIUS - 1; // it is faster to add this here then on every signle out of bounds check, is deducted when wrapping / bouncing
}

void ParticleSystem::setWrapX(bool enable)
{
	particlesettings.wrapX = enable;
}

void ParticleSystem::setWrapY(bool enable)
{
	particlesettings.wrapY = enable;
}

void ParticleSystem::setBounceX(bool enable)
{
	particlesettings.bounceX = enable;
}

void ParticleSystem::setBounceY(bool enable)
{
	particlesettings.bounceY = enable;
}

void ParticleSystem::setKillOutOfBounds(bool enable)
{
	particlesettings.killoutofbounds = enable;
}

// enable/disable gravity, optionally, set the force (force=8 is default) can be 1-255, 0 is also disable
// if enabled, gravity is applied to all particles in ParticleSystemUpdate()
void ParticleSystem::enableGravity(bool enable, uint8_t force) 
{
	particlesettings.useGravity = enable;
	if (force > 0)
		gforce = force;
	else 
		particlesettings.useGravity = false;
	
}

void ParticleSystem::enableParticleCollisions(bool enable, uint8_t hardness) // enable/disable gravity, optionally, set the force (force=8 is default) can be 1-255, 0 is also disable
{
	particlesettings.useCollisions = enable;
	collisionHardness = hardness + 1;
}

int16_t ParticleSystem::getMaxParticles(void)
{
	return numParticles;
}
	
// Spray emitter for particles used for flames (particle TTL depends on source TTL)
void ParticleSystem::FlameEmit(PSsource &emitter)
{
	for (uint32_t i = 0; i < usedParticles; i++)
	{
		emitIndex++;
		if (emitIndex >= usedParticles)
			emitIndex = 0;
		if (particles[emitIndex].ttl == 0) // find a dead particle
		{
			particles[emitIndex].x = emitter.source.x + random16(PS_P_RADIUS) - PS_P_HALFRADIUS; // jitter the flame by one pixel to make the flames wider and softer
			particles[emitIndex].y = emitter.source.y;
			particles[emitIndex].vx = emitter.vx + random16(emitter.var) - (emitter.var >> 1);
			particles[emitIndex].vy = emitter.vy + random16(emitter.var) - (emitter.var >> 1);
			particles[emitIndex].ttl = random16(emitter.maxLife - emitter.minLife) + emitter.minLife + emitter.source.ttl; // flame intensity dies down with emitter TTL  
			// fire uses ttl and not hue for heat, so no need to set the hue
			break; //done
		}
	}
}

// emit one particle with variation
void ParticleSystem::SprayEmit(PSsource &emitter)
{
	for (uint32_t i = 0; i < usedParticles; i++)
	{
		emitIndex++;
		if (emitIndex >= usedParticles)
			emitIndex = 0;
		if (particles[emitIndex].ttl == 0) // find a dead particle
		{
			particles[emitIndex].x = emitter.source.x; // + random16(emitter.var) - (emitter.var >> 1); //randomness uses cpu cycles and is almost invisible, removed for now.
			particles[emitIndex].y = emitter.source.y; // + random16(emitter.var) - (emitter.var >> 1);
			particles[emitIndex].vx = emitter.vx + random16(emitter.var) - (emitter.var>>1);
			particles[emitIndex].vy = emitter.vy + random16(emitter.var) - (emitter.var>>1);
			particles[emitIndex].ttl = random16(emitter.maxLife - emitter.minLife) + emitter.minLife;
			particles[emitIndex].hue = emitter.source.hue;
			particles[emitIndex].sat = emitter.source.sat;
			break;
		}
		/*	
		if (emitIndex < 2)
		{
		Serial.print(" ");
		Serial.print(particles[emitIndex].ttl);
		Serial.print(" ");
		Serial.print(particles[emitIndex].x);
		Serial.print(" ");
		Serial.print(particles[emitIndex].y);
		}*/
	}
	//Serial.println("**");
}

//todo: idee: man könnte einen emitter machen, wo die anzahl emittierten partikel von seinem alter abhängt. benötigt aber einen counter
//idee2: source einen counter hinzufügen, dann setting für emitstärke, dann müsste man das nicht immer in den FX animationen handeln

// Emits a particle at given angle and speed, angle is from 0-255 (=0-360deg), speed is also affected by emitter->var
// angle = 0 means in x-direction
void ParticleSystem::AngleEmit(PSsource &emitter, uint8_t angle, uint32_t speed)
{
	//todo: go to 16 bits, rotating particles could use this, others maybe as well
	emitter.vx = (((int32_t)cos8(angle) - 127) * speed) >> 7; // cos is signed 8bit, so 1 is 127, -1 is -127, shift by 7
	emitter.vy = (((int32_t)sin8(angle) - 127) * speed) >> 7;
	SprayEmit(emitter);
}

// particle moves, decays and dies, if killoutofbounds is set, out of bounds particles are set to ttl=0
// uses passed settings to set bounce or wrap, if useGravity is set, it will never bounce at the top
void ParticleSystem::ParticleMoveUpdate(PSparticle &part, PSsettings &options)
{
	if (part.ttl > 0)
	{
		// age
		part.ttl--;
		// apply velocity
		int32_t newX, newY; //use temporary 32bit vaiable to make function a tad faster (maybe)
		newX = part.x + (int16_t)part.vx;
		newY = part.y + (int16_t)part.vy;
		part.outofbounds = 0;	// reset out of bounds (in case particle was created outside the matrix and is now moving into view)

		if (((newX < -PS_P_HALFRADIUS) || (newX > maxX))) // check if particle is out of bounds
		{
			if (options.killoutofbounds)
				part.ttl = 0;
			else if (options.bounceX) // particle was in view and now moved out -> bounce it
			{					
				newX = -newX;	// invert speed
				newX = ((newX) * wallHardness) >> 8; // reduce speed as energy is lost on non-hard surface
				if (newX < 0)
					newX = -newX;					
				else
					newX = maxX - PS_P_RADIUS - newX;
			}
			else if (options.wrapX)
			{
				newX = wraparound(newX, maxX - PS_P_RADIUS);
			}
			else
				part.outofbounds = 1;
		}
	
		if (((newY < -PS_P_HALFRADIUS) || (newY > maxY))) // check if particle is out of bounds
		{
			if (options.killoutofbounds)
				part.ttl = 0;
			else if (options.bounceY) // particle was in view and now moved out -> bounce it
			{
				part.vy = -part.vy;						 // invert speed
				part.vy = (part.vy * wallHardness) >> 8; // reduce speed as energy is lost on non-hard surface
				if (newY < 0)
					newY = -newY;
				else if (options.useGravity == false) //if gravity disabled also bounce at the top
					newY = maxY - PS_P_RADIUS - newY;
			}
			else if (options.wrapY)
			{
				newY = wraparound(newY, maxY - PS_P_RADIUS);
			}
			else
				part.outofbounds = 1;
		}
		
			part.x = newX; // set new position
			part.y = newY; // set new position
	}
}

// apply a force in x,y direction to particles
// caller needs to provide a 8bit counter that holds its value between calls for each group (numparticles can be 1 for single particle)
// force is in 3.4 fixed point notation so force=16 means apply v+1 each frame default of 8 is every other frame (gives good results)
void ParticleSystem::applyForce(PSparticle *part, uint32_t numparticles, int8_t xforce, int8_t yforce, uint8_t *counter)
{
	// for small forces, need to use a delay counter
	uint8_t xcounter = (*counter) & 0x0F; // lower four bits
	uint8_t ycounter = (*counter) >> 4;	  // upper four bits

	// velocity increase
	int32_t dvx = calcForce_dV(xforce, &xcounter);
	int32_t dvy = calcForce_dV(yforce, &ycounter);

	// save counter values back
	*counter |= xcounter & 0x0F;		// write lower four bits, make sure not to write more than 4 bits
	*counter |= (ycounter << 4) & 0xF0; // write upper four bits

	// apply the force to particle:
	int32_t i = 0;
	if (dvx != 0)
	{
		if (numparticles == 1) // for single particle, skip the for loop to make it faster
		{
			particles[0].vx = particles[0].vx + dvx > PS_P_MAXSPEED ? PS_P_MAXSPEED : particles[0].vx + dvx; // limit the force, this is faster than min or if/else
		}
		else
		{
			for (i = 0; i < numparticles; i++)
			{
				// note: not checking if particle is dead is faster as most are usually alive and if few are alive, rendering is faster so no speed penalty
				particles[i].vx = particles[i].vx + dvx > PS_P_MAXSPEED ? PS_P_MAXSPEED : particles[i].vx + dvx; 
			}
		}
	}
	if (dvy != 0)
	{
		if (numparticles == 1) // for single particle, skip the for loop to make it faster
			particles[0].vy = particles[0].vy + dvy > PS_P_MAXSPEED ? PS_P_MAXSPEED : particles[0].vy + dvy;
		else
		{
			for (i = 0; i < numparticles; i++)
			{
				particles[i].vy = particles[i].vy + dvy > PS_P_MAXSPEED ? PS_P_MAXSPEED : particles[i].vy + dvy;
			}
		}
	}
}

// apply a force in angular direction to of particles
// caller needs to provide a 8bit counter that holds its value between calls for each group (numparticles can be 1 for single particle)
void ParticleSystem::applyAngleForce(PSparticle *part, uint32_t numparticles, uint8_t force, uint8_t angle, uint8_t *counter)
{
	int8_t xforce = ((int32_t)force * (cos8(angle) - 128)) >> 8; // force is +/- 127
	int8_t yforce = ((int32_t)force * (sin8(angle) - 128)) >> 8;
	// noste: sin16 is 10% faster than sin8() on ESP32 but on ESP8266 it is 9% slower, and dont need that 16bit of resolution
	// force is in 3.4 fixed point notation so force=16 means apply v+1 each frame (useful force range is +/- 127)
	applyForce(part, numparticles, xforce, yforce, counter);
}

// apply gravity to a group of particles
// faster than apply force since direction is always down and counter is fixed for all particles
// caller needs to provide a 8bit counter that holds its value between calls
// force is in 4.4 fixed point notation so force=16 means apply v+1 each frame default of 8 is every other frame (gives good results), force above 127 are VERY strong
void ParticleSystem::applyGravity(PSparticle *part, uint32_t numarticles, uint8_t force, uint8_t *counter)
{
	int32_t dv; // velocity increase

	if (force > 15)
		dv = (force >> 4); // apply the 4 MSBs
	else
		dv = 1;

	*counter += force;

	if (*counter > 15)
	{
		*counter -= 16;
		// apply force to all used particles
		for (uint32_t i = 0; i < numarticles; i++)
		{
			// note: not checking if particle is dead is faster as most are usually alive and if few are alive, rendering is fast anyways
			particles[i].vy = particles[i].vy - dv > PS_P_MAXSPEED ? PS_P_MAXSPEED : particles[i].vy - dv; // limit the force, this is faster than min or if/else
		}
	}
}

//apply gravity using PS global gforce
void ParticleSystem::applyGravity(PSparticle *part, uint32_t numarticles, uint8_t *counter)
{
	applyGravity(part, numarticles, gforce, counter);
}

// slow down particles by friction, the higher the speed, the higher the friction. a high friction coefficient slows them more (255 means instant stop)
void ParticleSystem::applyFriction(PSparticle *part, uint32_t numparticles, uint8_t coefficient)
{
	int32_t friction = 256 - coefficient;
	for (uint32_t i = 0; i < numparticles; i++)
	{
		// note: not checking if particle is dead is faster as most are usually alive and if few are alive, rendering is faster
		part[i].vx = ((int16_t)part[i].vx * friction) >> 8;
		part[i].vy = ((int16_t)part[i].vy * friction) >> 8;
	}
}

// TODO: attract needs to use the above force functions
// attracts a particle to an attractor particle using the inverse square-law
void ParticleSystem::attract(PSparticle *particle, PSparticle *attractor, uint8_t *counter, uint8_t strength, bool swallow)
{
	// Calculate the distance between the particle and the attractor
	int32_t dx = attractor->x - particle->x;
	int32_t dy = attractor->y - particle->y;

	// Calculate the force based on inverse square law
	int32_t distanceSquared = dx * dx + dy * dy + 1;
	if (distanceSquared < 4096)
	{
		if (swallow) // particle is close, kill it
		{
			particle->ttl = 0;
			return;
		}
		distanceSquared = 4 * PS_P_RADIUS * PS_P_RADIUS; // limit the distance of particle size to avoid very high forces
	}

	int32_t shiftedstrength = (int32_t)strength << 16;
	int32_t force;
	int32_t xforce;
	int32_t yforce;
	int32_t xforce_abs; // absolute value
	int32_t yforce_abs;

	force = shiftedstrength / distanceSquared;
	xforce = (force * dx) >> 10; // scale to a lower value, found by experimenting
	yforce = (force * dy) >> 10;
	xforce_abs = abs(xforce); // absolute value
	yforce_abs = abs(yforce);

	uint8_t xcounter = (*counter) & 0x0F; // lower four bits
	uint8_t ycounter = (*counter) >> 4;	  // upper four bits

	*counter = 0; // reset counter, is set back to correct values below

	// for small forces, need to use a delay timer (counter)
	if (xforce_abs < 16)
	{
		xcounter += xforce_abs;
		if (xcounter > 15)
		{
			xcounter -= 16;
			*counter |= xcounter & 0x0F; // write lower four bits, make sure not to write more than 4 bits
										 // apply force in x direction
			if (dx < 0)
				particle->vx -= 1;
			else
				particle->vx += 1;
		}
		else							 // save counter value
			*counter |= xcounter & 0x0F; // write lower four bits, make sure not to write more than 4 bits
	}
	else
	{
		particle->vx += xforce >> 4; // divide by 16
	}

	if (yforce_abs < 16)
	{
		ycounter += yforce_abs;

		if (ycounter > 15)
		{
			ycounter -= 16;
			*counter |= (ycounter << 4) & 0xF0; // write upper four bits

			if (dy < 0)
				particle->vy -= 1;
			else
				particle->vy += 1;
		}
		else									// save counter value
			*counter |= (ycounter << 4) & 0xF0; // write upper four bits
	}
	else
	{
		particle->vy += yforce >> 4; // divide by 16
	}
	// TODO: need to limit the max speed?
}


// render particles to the LED buffer (uses palette to render the 8bit particle color value)
// if wrap is set, particles half out of bounds are rendered to the other side of the matrix
void ParticleSystem::ParticleSys_render()
{
#ifdef ESP8266
	const bool fastcoloradd = true; // on ESP8266, we need every bit of performance we can get
#else
	const bool fastcoloradd = false; // on ESP32, there is very little benefit from using fast add
#endif


	int32_t pixelCoordinates[4][2]; //physical coordinates of the four positions, x,y pairs
	//int32_t intensity[4];
	CRGB baseRGB;
	uint32_t i;
	uint32_t brightness; // particle brightness, fades if dying
	//CRGB colorbuffer[maxXpixel/4][maxYpixel/4] = {0}; //put buffer on stack, will this work? or better allocate it? -> crashes hard even with quarter the size

	// to create a 2d array on heap:
/*
TODO: using a local buffer crashed immediately, find out why.
	//  Allocate memory for the array of pointers to rows
	CRGB **colorbuffer = (CRGB **)calloc(maxXpixel+1, sizeof(CRGB *));
	if (colorbuffer == NULL)
	{
		Serial.println("Memory allocation failed111");
		return;
	}

	// Allocate memory for each row
	for (i = 0; i < maxXpixel; i++)
	{
		colorbuffer[i] = (CRGB *)calloc(maxYpixel + 1, sizeof(CRGB));
		if (colorbuffer[i] == NULL)
		{
			Serial.println("Memory allocation failed222");
			return;
		}
	}*/

//TODO: in der renderfunktion gibts noch ein bug, am linken rand verschwindet die rechte hälfte der partikel sehr abrupt, das passiert auch wenn man TTX und outofbounds pixel mitrendert (continue unten auskommentiert)
//es hat also nichts mit dem TTL oder dem outofbounds zu tun sondern muss etwas anderes sein...
//rechts und oben gibts ein schönes fade-out der pixel, links und unten verschwinden sie plötzlich muss in der pixel renderfunktion sein.


	// go over particles and update matrix cells on the way
	for (i = 0; i < usedParticles; i++)
	{	
		/*
		if (particles[i].ttl == 0 || particles[i].outofbounds)
		{
			continue;
		}*/
		if (particles[i].ttl == 0)
		{
			continue;
		}
		// generate RGB values for particle
		brightness = particles[i].ttl > 255 ? 255 : particles[i].ttl; //faster then using min()
		baseRGB = ColorFromPalette(SEGPALETTE, particles[i].hue, 255, LINEARBLEND);
		if (particles[i].sat < 255)
		{
			CHSV baseHSV = rgb2hsv_approximate(baseRGB); //convert to hsv
			baseHSV.s = particles[i].sat; //desaturate
			baseRGB = (CRGB)baseHSV; //convert back to RGB
		}
		int32_t intensity[4] = {0}; //note: intensity needs to be set to 0 or checking in rendering function does not work (if values persist), this is faster then setting it to 0 there

		// calculate brightness values for all four pixels representing a particle using linear interpolation and calculate the coordinates of the phyiscal pixels to add the color to
		renderParticle(&particles[i], brightness, intensity, pixelCoordinates);

		if (intensity[0] > 0)
		SEGMENT.addPixelColorXY(pixelCoordinates[0][0], maxYpixel - pixelCoordinates[0][1], baseRGB.scale8((uint8_t)intensity[0]), fastcoloradd); // bottom left
		if (intensity[1] > 0)
		SEGMENT.addPixelColorXY(pixelCoordinates[1][0], maxYpixel - pixelCoordinates[1][1], baseRGB.scale8((uint8_t)intensity[1]), fastcoloradd); // bottom right
		if (intensity[2] > 0)
		SEGMENT.addPixelColorXY(pixelCoordinates[2][0], maxYpixel - pixelCoordinates[2][1], baseRGB.scale8((uint8_t)intensity[2]), fastcoloradd); // top right
		if (intensity[3] > 0)
		SEGMENT.addPixelColorXY(pixelCoordinates[3][0], maxYpixel - pixelCoordinates[3][1], baseRGB.scale8((uint8_t)intensity[3]), fastcoloradd); // top left

		//test to render larger pixels with minimal effort (not working yet, need to calculate coordinate from actual dx position but brightness seems right)
		//	SEGMENT.addPixelColorXY(pixelCoordinates[1][0] + 1, maxYpixel - pixelCoordinates[1][1], baseRGB.scale8((uint8_t)((brightness>>1) - intensity[0])), fastcoloradd); 																																									  
		//	SEGMENT.addPixelColorXY(pixelCoordinates[2][0] + 1, maxYpixel - pixelCoordinates[2][1], baseRGB.scale8((uint8_t)((brightness>>1) -intensity[3])), fastcoloradd);
		//	colorbuffer[pixelCoordinates[0][0]][maxYpixel - pixelCoordinates[0][1]] += baseRGB.scale8((uint8_t)intensity[0]);
		//	colorbuffer[pixelCoordinates[1][0]][maxYpixel - pixelCoordinates[1][1]] += baseRGB.scale8((uint8_t)intensity[0]);
		//	colorbuffer[pixelCoordinates[2][0]][maxYpixel - pixelCoordinates[2][1]] += baseRGB.scale8((uint8_t)intensity[0]);
		//	colorbuffer[pixelCoordinates[3][0]][maxYpixel - pixelCoordinates[3][1]] += baseRGB.scale8((uint8_t)intensity[0]);
	}
	/*
	int x,y;
	for (x = 0; x <= maxXpixel; x++)
	{
		for (y = 0; x <= maxYpixel; y++)
		{
			if(colorbuffer[x][y]>0)
			{
				SEGMENT.setPixelColorXY(x,y,colorbuffer[x][y]);
			}
		}
	}

	// Free memory for each row
	for (int i = 0; i <= maxXpixel; i++)
	{
		free(colorbuffer[i]);
	}

	// Free memory for the array of pointers to rows
	free(colorbuffer);*/
}

// calculate pixel positions and brightness distribution for rendering function
// pixelpositions are the physical positions in the matrix that the particle renders to (4x2 array for the four positions)

void ParticleSystem::renderParticle(PSparticle* particle, uint32_t brightess, int32_t *pixelvalues, int32_t (*pixelpositions)[2])
{
	// subtract half a radius as the rendering algorithm always starts at the bottom left, this makes calculations more efficient
	int32_t xoffset = particle->x - PS_P_HALFRADIUS;
	int32_t yoffset = particle->y - PS_P_HALFRADIUS;
	int32_t dx = xoffset % PS_P_RADIUS; //relativ particle position in subpixel space
	int32_t dy = yoffset % PS_P_RADIUS;
	int32_t x = xoffset >> PS_P_RADIUS_SHIFT; // divide by PS_P_RADIUS which is 64, so can bitshift (compiler may not optimize automatically)
	int32_t y = yoffset >> PS_P_RADIUS_SHIFT;

	// set the four raw pixel coordinates, the order is bottom left [0], bottom right[1], top right [2], top left [3]
	pixelpositions[0][0] = pixelpositions[3][0] = x;	 // bottom left & top left
	pixelpositions[0][1] = pixelpositions[1][1] = y;	 // bottom left & bottom right
	pixelpositions[1][0] = pixelpositions[2][0] = x + 1; // bottom right & top right
	pixelpositions[2][1] = pixelpositions[3][1] = y + 1; // top right & top left

	// now check if any are out of frame. set values to -1 if they are so they can be easily checked after (no value calculation, no setting of pixelcolor if value < 0)	
	
	if (x < 0) // left pixels out of frame
	{
		dx = PS_P_RADIUS + dx;		// if x<0, xoffset becomes negative (and so does dx), must adjust dx as modulo will flip its value (really old bug now finally fixed)
		//note: due to inverted shift math, a particel at position -32 (xoffset = -64, dx = 64) is rendered at the wrong pixel position (it should be out of frame)
		//checking this above makes this algorithm slower (in frame pixels do not have to be checked), so just correct for it here:
		if (dx == PS_P_RADIUS)
		{
			pixelvalues[1] = pixelvalues[2] = -1; // pixel is actually out of matrix boundaries, do not render
		}
		if (particlesettings.wrapX) // wrap x to the other side if required
			pixelpositions[0][0] = pixelpositions[3][0] = maxXpixel;
		else
			pixelvalues[0] = pixelvalues[3] = -1; // pixel is out of matrix boundaries, do not render
	}
	else if (pixelpositions[1][0] > maxXpixel) // right pixels, only has to be checkt if left pixels did not overflow
	{
		if (particlesettings.wrapX) // wrap y to the other side if required
			pixelpositions[1][0] = pixelpositions[2][0] = 0;
		else
			pixelvalues[1] = pixelvalues[2] = -1;
	}

	if (y < 0) // bottom pixels out of frame
	{
		dy = PS_P_RADIUS + dy; //see note above
		if (dy == PS_P_RADIUS)
		{
			pixelvalues[2] = pixelvalues[3] = -1; // pixel is actually out of matrix boundaries, do not render
		}
		if (particlesettings.wrapY) // wrap y to the other side if required
			pixelpositions[0][1] = pixelpositions[1][1] = maxYpixel;
		else
			pixelvalues[0] = pixelvalues[1] = -1;
	}
	else if (pixelpositions[2][1] > maxYpixel) // top pixels
	{
		if (particlesettings.wrapY) // wrap y to the other side if required
			pixelpositions[2][1] = pixelpositions[3][1] = 0;
		else
			pixelvalues[2] = pixelvalues[3] = -1;
	}


	// calculate brightness values for all four pixels representing a particle using linear interpolation
	// precalculate values for speed optimization
	int32_t precal1 = (int32_t)PS_P_RADIUS - dx;
	int32_t precal2 = ((int32_t)PS_P_RADIUS - dy) * brightess;
	int32_t precal3 = dy * brightess;

	//calculate the values for pixels that are in frame
	if (pixelvalues[0] >= 0)
		pixelvalues[0] = (precal1 * precal2) >> PS_P_SURFACE; // bottom left value equal to ((PS_P_RADIUS - dx) * (PS_P_RADIUS-dy) * brightess) >> PS_P_SURFACE
	if (pixelvalues[1] >= 0)
		pixelvalues[1] = (dx * precal2) >> PS_P_SURFACE; // bottom right value equal to (dx * (PS_P_RADIUS-dy) * brightess) >> PS_P_SURFACE
	if (pixelvalues[2] >= 0)
		pixelvalues[2] = (dx * precal3) >> PS_P_SURFACE; // top right value equal to (dx * dy * brightess) >> PS_P_SURFACE
	if (pixelvalues[3] >= 0)
		pixelvalues[3] = (precal1 * precal3) >> PS_P_SURFACE; // top left value equal to ((PS_P_RADIUS-dx) * dy * brightess) >> PS_P_SURFACE
/*
	Serial.print(particle->x);
	Serial.print(" ");
	Serial.print(xoffset);
	Serial.print(" dx");
	Serial.print(dx);
	Serial.print(" ");
	for(uint8_t t = 0; t<4; t++)
	{
		Serial.print("x");
		Serial.print(pixelpositions[t][0]);
		Serial.print(" y");
		Serial.print(pixelpositions[t][1]);
		Serial.print(" v");		
		Serial.print(pixelvalues[t]);
		Serial.print(" ");
	}
	Serial.println(" ");
	*/
}

// update & move particle, wraps around left/right if settings.wrapX is true, wrap around up/down if settings.wrapY is true
// particles move upwards faster if ttl is high (i.e. they are hotter)
void ParticleSystem::FireParticle_update()
{
	//TODO: cleanup this function?
	uint32_t i = 0;

	for (i = 0; i < usedParticles; i++)
	{
		if (particles[i].ttl > 0)
		{
			// age
			particles[i].ttl--;

			// apply velocity
			particles[i].x = particles[i].x + (int32_t)particles[i].vx;
			particles[i].y = particles[i].y + (int32_t)particles[i].vy + (particles[i].ttl >> 4); // younger particles move faster upward as they are hotter, used for fire

			particles[i].outofbounds = 0;
			// check if particle is out of bounds, wrap x around to other side if wrapping is enabled
			// as fire particles start below the frame, lots of particles are out of bounds in y direction. to improve animation speed, only check x direction if y is not out of bounds
			// y-direction
			if (particles[i].y < -PS_P_HALFRADIUS)
			{
				particles[i].outofbounds = 1;
			}
			else if (particles[i].y > maxY) // particle moved out on the top
			{
				particles[i].ttl = 0;
			}
			else // particle is in frame in y direction, also check x direction now
			{
				if ((particles[i].x < -PS_P_HALFRADIUS) || (particles[i].x > maxX))
				{
					if (particlesettings.wrapX)
					{
						particles[i].x = wraparound(particles[i].x, maxX);						
					}
					else
					{
						particles[i].ttl = 0;
					}
				}
			}
		}
	}
}

// render fire particles to the LED buffer using heat to color
// each particle adds heat according to its 'age' (ttl) which is then rendered to a fire color in the 'add heat' function
// note: colormode 0-5 are native, heat based color modes, set colormode to 255 to use palette
void ParticleSystem::renderParticleFire(uint8_t colormode)
{
//TODO: if colormode = 255 call normal rendering function
	int32_t pixelCoordinates[4][2]; // physical coordinates of the four positions, x,y pairs
	int32_t pixelheat[4];
	uint32_t flameheat; //depends on particle.ttl
	uint32_t i;
	
	// go over particles and update matrix cells on the way
	// note: some pixels (the x+1 ones) can be out of bounds, it is probably faster than to check that for every pixel as this only happens on the right border (and nothing bad happens as this is checked down the road)
	for (i = 0; i < usedParticles; i++)
	{
		if (particles[i].outofbounds) //lots of fire particles are out of bounds, check first
			continue;

		if (particles[i].ttl == 0)
			continue;

		flameheat = particles[i].ttl;
		renderParticle(&particles[i], flameheat, pixelheat, pixelCoordinates);


		//TODO: add one more pixel closer to the particle, so it is 3 pixels wide

		if (pixelheat[0] >= 0)
			PartMatrix_addHeat(pixelCoordinates[0][0], pixelCoordinates[0][1], pixelheat[0], colormode);
		if (pixelheat[1] >= 0)
			PartMatrix_addHeat(pixelCoordinates[1][0], pixelCoordinates[1][1], pixelheat[0], colormode);
		if (pixelheat[2] >= 0)
			PartMatrix_addHeat(pixelCoordinates[2][0], pixelCoordinates[2][1], pixelheat[0], colormode);
		if (pixelheat[3] >= 0)
			PartMatrix_addHeat(pixelCoordinates[3][0], pixelCoordinates[3][1], pixelheat[0], colormode);

		// TODO: add heat to a third pixel. need to konw dx and dy, the heatvalue is (flameheat - pixelheat) vom pixel das weiter weg ist vom partikelzentrum
		// also wenn dx < halfradius dann links, sonst rechts. rechts flameheat-pixelheat vom linken addieren und umgekehrt
		// das ist relativ effizient um rechnen und sicher schneller als die alte variante. gibt ein FPS drop, das könnte man aber
		// mit einer schnelleren add funktion im segment locker ausgleichen
	}
}

// adds 'heat' to red color channel, if it overflows, add it to next color channel
// colormode is 0-5 where 0 is normal fire and all others are color variations
void ParticleSystem::PartMatrix_addHeat(uint8_t col, uint8_t row, uint32_t heat, uint8_t colormode)
{

	CRGB currentcolor = SEGMENT.getPixelColorXY(col, maxYpixel - row); // read current matrix color (flip y axis)
	uint32_t newcolorvalue, i;

	// define how the particle TTL value (which is the heat given to the function) maps to heat, if lower, fire is more red, if higher, fire is brighter as bright flames travel higher and decay faster
	// need to scale ttl value of particle to a good heat value that decays fast enough
	#ifdef ESP8266
	heat = heat << 4; //ESP8266 has slow hardware multiplication, just use shift (also less particles, need more heat)
	#else
	heat = heat * 10; //TODO: need to play with this some more to see if it makes fire better or worse
	#endif

	uint32_t coloridx = (colormode & 0x07) >> 1; // set startindex for colormode 0 is normal red fire, 1 is green fire, 2 is blue fire
	if (coloridx > 2)
		coloridx -= 3;	// faster than i = i % 3
	uint32_t increment = (colormode & 0x01) + 1; // 0 (or 3) means only one single color for the flame, 1 is normal, 2 is alternate color modes
	//go over the three colors and fill them with heat, if one overflows, add heat to the next
	for (i = 0; i < 3; ++i)
	{
		if (currentcolor[coloridx] < 255) //current color is not yet full
		{
				if (heat > 255)
				{
					heat -= 255 - currentcolor[coloridx];
					currentcolor[coloridx] = 255;
				}
				else{
					int32_t leftover = heat - currentcolor[coloridx];
					if(leftover <= 0)
					{
						currentcolor[coloridx] += heat;
						break;
					}
					else{
						currentcolor[coloridx] = 255;
						if(heat > leftover)
						{
							heat -= leftover;
						}
						else
							break;
					}
				}					
		}
		coloridx += increment; 
		if (coloridx > 2)
			coloridx -= 3; // faster than i = i % 3 and is allowed since increment is never more than 2
	}

	if (i == 2) // last color was reached limit the color value (in normal mode, this is blue) so it does not go full white
	{
		currentcolor[coloridx] = currentcolor[coloridx] > 60 ? 60 : currentcolor[coloridx]; //faster than min()
	}

	SEGMENT.setPixelColorXY(col, maxYpixel - row, currentcolor);
}

// detect collisions in an array of particles and handle them
void ParticleSystem::handleCollisions()
{
	// detect and handle collisions
	uint32_t i, j;
	uint32_t startparticle = 0;
	uint32_t endparticle = usedParticles >> 1; // do half the particles, significantly speeds things up

	// every second frame, do other half of particles (helps to speed things up as not all collisions are handled each frame, less accurate but good enough)
	// if m ore accurate collisions are needed, just call it twice in a row
	if (collisioncounter & 0x01) 
	{ 
		startparticle = endparticle;
		endparticle = usedParticles;
	}
	collisioncounter++;

	for (i = startparticle; i < endparticle; i++)
	{
		// go though all 'higher number' particles and see if any of those are in close proximity and if they are, make them collide
		if (particles[i].ttl > 0 && particles[i].outofbounds == 0) // if particle is alive and does collide and is not out of view
		{
			int32_t dx, dy; // distance to other particles
			for (j = i + 1; j < usedParticles; j++)
			{							  // check against higher number particles
				if (particles[j].ttl > 0) // if target particle is alive
				{
					dx = particles[i].x - particles[j].x;
					if (dx < PS_P_HARDRADIUS && dx > -PS_P_HARDRADIUS) // check x direction, if close, check y direction
					{
						dy = particles[i].y - particles[j].y;
						if (dy < PS_P_HARDRADIUS && dy > -PS_P_HARDRADIUS) // particles are close
							collideParticles(&particles[i], &particles[j]);
					}
				}
			}
		}
	}
}

// handle a collision if close proximity is detected, i.e. dx and/or dy smaller than 2*PS_P_RADIUS
// takes two pointers to the particles to collide and the particle hardness (softer means more energy lost in collision, 255 means full hard)
void ParticleSystem::collideParticles(PSparticle *particle1, PSparticle *particle2)
{

	int32_t dx = particle2->x - particle1->x;
	int32_t dy = particle2->y - particle1->y;
	int32_t distanceSquared = dx * dx + dy * dy;

	// Calculate relative velocity (if it is zero, could exit but extra check does not overall speed but deminish it)
	int32_t relativeVx = (int16_t)particle2->vx - (int16_t)particle1->vx;
	int32_t relativeVy = (int16_t)particle2->vy - (int16_t)particle1->vy;


	if (distanceSquared == 0) // add distance in case particles exactly meet at center, prevents dotProduct=0 (this can only happen if they move towards each other)
	{
		// Adjust positions based on relative velocity direction TODO: is this really needed? only happens on fast particles, would save some code (but make it a tiny bit less accurate on fast particles but probably not an issue)

		if (relativeVx < 0)
		{ // if true, particle2 is on the right side
			particle1->x--;
			particle2->x++;
		}
		else
		{
			particle1->x++;
			particle2->x--;
		}

		if (relativeVy < 0)
		{
			particle1->y--;
			particle2->y++;
		}
		else
		{
			particle1->y++;
			particle2->y--;
		}
		distanceSquared++;
	}
	// Calculate dot product of relative velocity and relative distance
	int32_t dotProduct = (dx * relativeVx + dy * relativeVy);

	// If particles are moving towards each other
	if (dotProduct < 0)
	{
		const uint32_t bitshift = 14; // bitshift used to avoid floats

		// Calculate new velocities after collision
		int32_t impulse = (((dotProduct << (bitshift)) / (distanceSquared)) * collisionHardness) >> 8;
		int32_t ximpulse = (impulse * dx) >> bitshift;
		int32_t yimpulse = (impulse * dy) >> bitshift;
		particle1->vx += ximpulse;
		particle1->vy += yimpulse;
		particle2->vx -= ximpulse;
		particle2->vy -= yimpulse;
		/*
		//TODO: this is removed for now as it does not seem to do much and does not help with piling. if soft, much energy is lost anyway at a collision, so they are automatically sticky
		//also second version using multiplication is slower on ESP8266 than the if's
		if (hardness < 50) // if particles are soft, they become 'sticky' i.e. they are slowed down at collisions 
		{
			
			//particle1->vx = (particle1->vx < 2 && particle1->vx > -2) ? 0 : particle1->vx;
			//particle1->vy = (particle1->vy < 2 && particle1->vy > -2) ? 0 : particle1->vy;

			//particle2->vx = (particle2->vx < 2 && particle2->vx > -2) ? 0 : particle2->vx;
			//particle2->vy = (particle2->vy < 2 && particle2->vy > -2) ? 0 : particle2->vy;

			const uint32_t coeff = 100;
			particle1->vx = ((int32_t)particle1->vx * coeff) >> 8;
			particle1->vy = ((int32_t)particle1->vy * coeff) >> 8;

			particle2->vx = ((int32_t)particle2->vx * coeff) >> 8;
			particle2->vy = ((int32_t)particle2->vy * coeff) >> 8;
		}*/
	}

	// particles have volume, push particles apart if they are too close by moving each particle by a fixed amount away from the other particle
	// if pushing is made dependent on hardness, things start to oscillate much more, better to just add a fixed, small increment (tried lots of configurations, this one works best)
	// one problem remaining is, particles get squished if (external) force applied is higher than the pushback but this may also be desirable if particles are soft. also some oscillations cannot be avoided without addigng a counter
	if (distanceSquared < 2 * PS_P_HARDRADIUS * PS_P_HARDRADIUS)
	{
		uint8_t choice = dotProduct & 0x01; // random16(2); // randomly choose one particle to push, avoids oscillations note: dotprouct LSB should be somewhat random, so no need to calculate a random number
		const int32_t HARDDIAMETER = 2 * PS_P_HARDRADIUS;
		const int32_t pushamount = 2; //push a small amount
		int32_t push = pushamount;

		if (dx < HARDDIAMETER && dx > -HARDDIAMETER)
		{ // distance is too small, push them apart

			if (dx <= 0)
				push = -pushamount; //-(PS_P_HARDRADIUS + dx); // inverted push direction

			if (choice) // chose one of the particles to push, avoids oscillations
				particle1->x -= push;
			else
				particle2->x += push;
		}

		push = pushamount; // reset push variable to 1
		if (dy < HARDDIAMETER && dy > -HARDDIAMETER)
		{
			if (dy <= 0)
				push = -pushamount; //-(PS_P_HARDRADIUS + dy); // inverted push direction

			if (choice) // chose one of the particles to push, avoids oscillations
				particle1->y -= push;
			else
				particle2->y += push;
		}
		// note: pushing may push particles out of frame, if bounce is active, it will move it back as position will be limited to within frame, if bounce is disabled: bye bye
	}
}

//fast calculation of particle wraparound (modulo version takes 37 instructions, this only takes 28, other variants are slower on ESP8266)
//function assumes that out of bounds is checked before calling it
int32_t ParticleSystem::wraparound(int32_t p, int32_t maxvalue)
{
	if (p < 0)
	{
		p += maxvalue + 1;
	}
	else //if (p > maxvalue) 
	{
		p -= maxvalue + 1;
	}
	return p;
}

//calculate the dV value and update the counter for force calculation (is used several times, function saves on codesize)
//force is in 3.4 fixedpoint notation, +/-127
int32_t ParticleSystem::calcForce_dV(int8_t force, uint8_t* counter)
{
	// for small forces, need to use a delay counter
	int32_t force_abs = abs(force); // absolute value (faster than lots of if's only 7 instructions)
	int32_t dv;
	// for small forces, need to use a delay counter, apply force only if it overflows
	if (force_abs < 16)
	{
		*counter += force_abs;
		if (*counter > 15)
		{
			*counter -= 16;
			dv = (force < 0) ? -1 : ((force > 0) ? 1 : 0); // force is either, 1, 0 or -1 if it is small
		}		
	}
	else
	{
		dv = force >> 4; // MSBs
	}
	return dv;
}

// set the pointers for the class (this only has to be done once and not on every FX call, only the class pointer needs to be reassigned to SEGENV.data every time)
// function returns the pointer to the next byte available for the FX (if it assigned more memory for other stuff using the above allocate function)
// FX handles the PSsources, need to tell this function how many there are
void ParticleSystem::setPSpointers(uint16_t numsources)
{
	particles = reinterpret_cast<PSparticle *>(SEGMENT.data + sizeof(ParticleSystem)); // pointer to particle array
	sources = reinterpret_cast<PSsource *>(particles + numParticles);				  // pointer to source(s)
	PSdataEnd = reinterpret_cast<uint8_t *>(sources + numsources);					  // pointer to first available byte after the PS
}

//non class functions to use for initialization

uint32_t calculateNumberOfParticles()
{
	uint32_t cols = strip.isMatrix ? SEGMENT.virtualWidth() : 1;
	uint32_t rows = strip.isMatrix ? SEGMENT.virtualHeight() : SEGMENT.virtualLength();
#ifdef ESP8266
	uint32_t numberofParticles = cols * rows ; // 1 particle per pixel
#elseif ARDUINO_ARCH_ESP32S2
	uint32_t numberofParticles = (cols * rows * 3) / 2; // 1.5 particles per pixel (for example 768 particles on 32x16)
#else
	uint32_t numberofParticles = (cols * rows * 7) / 4; // 1.75 particles per pixel 
#endif

	Serial.print("segsize ");
	Serial.print(cols);
	Serial.print(" ");
	Serial.println(rows);
	// TODO: ist das genug für fire auf 32x16? evtl auf 2 gehen? oder das dynamisch machen, als parameter?
	return numberofParticles;
}

//allocate memory for particle system class, particles, sprays plus additional memory requested by FX
bool allocateParticleSystemMemory(uint16_t numparticles, uint16_t numsources, uint16_t additionalbytes)
{
	uint32_t requiredmemory = sizeof(ParticleSystem);
	requiredmemory += sizeof(PSparticle) * numparticles;
	requiredmemory += sizeof(PSsource) * numsources;
	requiredmemory += additionalbytes;
	return(SEGMENT.allocateData(requiredmemory));		
}

bool initParticleSystem(ParticleSystem *&PartSys, uint16_t numsources)
{
	Serial.println("PS init function");
	uint32_t numparticles = calculateNumberOfParticles();
	if (!allocateParticleSystemMemory(numparticles, numsources, 0))
	{
		DEBUG_PRINT(F("PS init failed: memory depleted"));
		return false;
	}
	Serial.println("memory allocated");
	uint16_t cols = strip.isMatrix ? SEGMENT.virtualWidth() : 1;
	uint16_t rows = strip.isMatrix ? SEGMENT.virtualHeight() : SEGMENT.virtualLength();
	Serial.println("calling constructor");
	PartSys = new (SEGMENT.data) ParticleSystem(cols, rows, numparticles, numsources); // particle system constructor
	Serial.print("PS pointer at ");
	Serial.println((uintptr_t)PartSys);
	return true;
}


