/*
 * Copyright 1999-2006 University of Chicago
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.globus.usage.report.rls;

/**
 * Represents an RLS entry in the usage statistics. All methods package 
 * accessible to allow quick and easy modification.
 */
class RLSEntry {

    String ver = "unkown";
    long uptime = 0;
    boolean lrc = false;
    boolean rli = false;
    int lfn = 0;
    int pfn = 0;
    int map = 0;
}