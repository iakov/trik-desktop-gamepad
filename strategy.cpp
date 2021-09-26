/* Copyright 2017 Konstantin Batoev.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file was modified by Konstantin Batoev to make it comply with the requirements of trikRuntime
 * project. See git revision history for detailed changes. */

#include "strategy.h"

#include "standardStrategy.h"
#include "accelerateStrategy.h"

// defining static variable
QMap<Strategies, QSharedPointer<Strategy> > Strategy::instances;

void Strategy::reset()
{
	mPressedKeys.clear();
}

Strategy *Strategy::getStrategy(Strategies type)
{
	if (instances.empty()) {
		createInstances();
	}

	return instances[type].data();
}

void Strategy::createInstances()
{
	instances.insert(standartStrategy, QSharedPointer<Strategy>(new StandardStrategy));
	instances.insert(accelerateStrategy, QSharedPointer<Strategy>(new AccelerateStrategy));
}
