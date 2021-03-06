#include "SBridger.h"
#include "globals.h"
#include <vector>
#include <algorithm>
// #include <string>
#include <iostream>
#include <set>

namespace scara {

  using namespace std;

  extern int multithreading;

  SBridger::SBridger(const string& strReadsFasta, const string& strContigsFasta, const string& strR2Cpaf, const string& strR2Rpaf) {
    Initialize(strReadsFasta, strContigsFasta, strR2Cpaf, strR2Rpaf);
    bGraphCreated = 0;
  }

  void SBridger::Initialize(const string& strReadsFasta, const string& strContigsFasta, const string& strR2Cpaf, const string& strR2Rpaf) {
      parseProcessFastq(strReadsFasta, mIdToRead);
      parseProcessFasta(strContigsFasta, mIdToContig);
      parseProcessPaf(strR2Cpaf, vOvlR2C);
      parseProcessPaf(strR2Rpaf, vOvlR2R);
  }

	void SBridger::printState() {
		std::cerr << "\nSBridger state:";
		
	  	std::cerr << "\nContigs map size: " << mIdToContig.size();
	  	std::cerr << "\nReads map size: " << mIdToRead.size();

	  	std::cerr << "\nContig-read ovelaps vector size: " << vOvlR2C.size();
	  	std::cerr << "\nRead-read ovelaps vector size: " << vOvlR2R.size();

		std::cerr << "\nAnchor nodes map size:" << mAnchorNodes.size();
		std::cerr << "\nRead nodes map size:" << mReadNodes.size();
	  	uint32_t numEdges = 0;
	  	for (auto const& it : mAnchorNodes) numEdges += it.second->vOutEdges.size();
	  	for (auto const& it : mReadNodes) numEdges += it.second->vOutEdges.size();
	  	std::cerr << "\nOutgoing edges: " << numEdges;
	  	// std::cerr << "\nAll edges: " << vEdges.size();

	  	std::cerr << "\nPaths vector size: " << vPaths.size();
	  	// Estimating the total size of paths
	  	numEdges = 0;
	  	for (auto const& path_ptr: vPaths) numEdges += path_ptr->edges.size();
	  	std::cerr << "\nTotal number of edges in paths: " << numEdges;

	  	std::cerr << "\nPath infos vector size: " << vPathInfos.size();
	  	std::cerr << "\nPath groups vector size: " << vPathGroups.size();

	  	std::cerr << "\nFinal scaffolds vector size: " << scaffolds.size();
	}


	void SBridger::printData(void){
	  std::cerr << "\nLoaded data\n";
      std::cerr << "Number of contigs:" << mIdToContig.size() << '\n';
      std::cerr << "Number of reads:" << mIdToRead.size() << '\n';
      std::cerr << "Number of read-to-contig overlaps:" << vOvlR2C.size() << '\n';
      std::cerr << "Number of read-to-read overlaps:" << vOvlR2R.size() << '\n';
	}


	void SBridger::printGraph(void){
		std::cerr << "\nConstructed graph:\n";
		if (bGraphCreated == 0) {
			std::cerr << "Graph is not yet constructed!\n";
		}
		else {
	        std::cerr << "Number of anchor nodes:" << mAnchorNodes.size() << '\n';
	        std::cerr << "Number of isolated anchor nodes:" << isolatedANodes << '\n';
	        std::cerr << "Number of read nodes:" << mReadNodes.size() << '\n';
	        std::cerr << "Number of isolated read nodes:" << isolatedRNodes << '\n';
	        uint32_t numEdges = 0;
	  		for (auto const& it : mAnchorNodes) numEdges += it.second->vOutEdges.size();
	  		for (auto const& it : mReadNodes) numEdges += it.second->vOutEdges.size();
	        std::cerr << "Number of edges:" << numEdges << '\n';

	        std::cerr << "\nEdges:" << '\n';
			std::cerr << "Usable: " << numEdges_usable << '\n';
			std::cerr << "Contained: " << numEdges_contained << '\n';
			std::cerr << "Short: " << numEdges_short << '\n';
			std::cerr << "Low quality: " << numEdges_lowqual << '\n';
			std::cerr << "Zero extension: " << numEdges_zero << '\n';

		}
	}


	void SBridger::printPaths(void) {
		std::cerr << "\nPrinting generated paths!";
		std::cerr << "\nNumber of paths: " << vPaths.size();
		std::cerr << "\nPaths:\n";
		int i = 1;

		for (auto const& t_path_ptr : vPaths) {
			size_t size = t_path_ptr->edges.size();
			std::cerr << "\nPath #" << i << ", edges: " << size << '\n';
			if (size > 10) {
				auto first = t_path_ptr->edges.front();
				auto last  = t_path_ptr->edges.back();
				std::cerr << '(' << first->getStartNodeName() << ", " << first->getEndNodeName() << ") ... ";
				std::cerr << '(' << last->getStartNodeName() << ", " << last->getEndNodeName() << ")\n";
			}
			else {
				for (auto const& t_edge_ptr : t_path_ptr->edges) {
					std::cerr << '(' << t_edge_ptr->getStartNodeName() << ", " << t_edge_ptr->getEndNodeName() << ") ";
				}	
			}

			i++;
		}
	}



  void SBridger::print(void) {
  	  ofstream outStream;
	  outStream.open(scara::logFile);
      std::cerr << "\nSBridger:\n";
      printData();
      printGraph();

      if (scara::globalDebugLevel >= DL_VERBOSE) {
		  outStream << "OVERLAPS FOR CONTIGS:" << endl << endl;
		  printOvlToStream(vOvlR2C, outStream);

		  outStream << endl << "OVERLAPS FOR READS:" << endl << endl;
		  printOvlToStream(vOvlR2R, outStream);

		  outStream << endl << "EDGES FOR ANCHOR NODES:" << endl << endl;
		  printNodeToStream(mAnchorNodes, outStream);

		  outStream << endl << "EDGES FOR READ NODES:" << endl << endl;
		  printNodeToStream(mReadNodes, outStream);
	  }

 	  outStream.close();
  }

  void SBridger::generateGraph(void) {

  	 numANodes = numRNodes = 0;

	 numEdges_all = numEdges_usable = numEdges_contained = numEdges_short = numEdges_lowqual = numEdges_zero = 0;

	// 1. Generate anchor nodes for each original contig and for reverse complement
	for (auto const& it : mIdToContig) {
		// Original contig
		auto node_ptr = make_shared<Node>(it.second, NT_ANCHOR, false);
		mAnchorNodes.emplace(it.first, node_ptr);

		// Reverse complement
		node_ptr = make_shared<Node>(it.second, NT_ANCHOR, true);
		mAnchorNodes.emplace(it.first + "_RC", node_ptr);
	}
	numANodes = mAnchorNodes.size();

	// 2. Generate read nodes for each read contig and for reverse complement
	for (auto const& it : mIdToRead) {
		// Original read
		auto node_ptr = make_shared<Node>(it.second, NT_READ, false);
		mReadNodes.emplace(it.first, node_ptr);

		// Reverse complement
		node_ptr = make_shared<Node>(it.second, NT_READ, true);
		mReadNodes.emplace(it.first + "_RC", node_ptr);
	}
	numRNodes = mReadNodes.size();

	// 3. Generate edges, function Overlap::Test() is used for filtering
	std::vector<shared_ptr<Edge>> vTempEdges;		// Temporarily store all edges here
	for (auto const& it : vOvlR2C) {
		if (it->Test()) {
			createEdgesFromOverlap(it, mAnchorNodes, mReadNodes, vTempEdges);
		}
	}
	
	for (auto const& it : vOvlR2R) {
		if (it->Test()) {
			createEdgesFromOverlap(it, mAnchorNodes, mReadNodes, vTempEdges);
		}
	}

	// Clear vectors with Overlaps, do not need them any more
	// TODO: Do not use overlaps at all, just use edges from the start.
	vOvlR2C.clear();
	vOvlR2C.shrink_to_fit();
	vOvlR2R.clear();
	vOvlR2R.shrink_to_fit();

	// 3.1 Connect Edges to Nodes
	for (auto const& edge_ptr : vTempEdges) {
		// First test edges
		int test_val = edge_ptr->test();
		switch (test_val) {
			case (-1):
				numEdges_contained += 1;
				break;
			case (-2):
				numEdges_short += 1;
				break;
			case (-3):
				numEdges_lowqual += 1;
				break;
			case (-4):
				numEdges_zero += 1;
				break;
			default:
				numEdges_usable += 1;
				break;
		}
		if (test_val > 0) {
			// Add edge to outgoing edges for its startNode
			std::shared_ptr<Node> startNode = edge_ptr->startNode;
			std::shared_ptr<Node> endNode = edge_ptr->endNode;
			startNode->vOutEdges.emplace_back(std::move(edge_ptr));
			/* KK: trying not to use that any more
			vEdges.emplace_back(edge_ptr);				// Only usable edges are stored in the vector
			*/
		}
	}
	vTempEdges.clear(); 
	vTempEdges.shrink_to_fit();

	// 4. Filter nodes
	// Remove isolated and contained read nodes
	// TODO:
	// Currently only calculating isolated Anchor and Read Nodes
	for (auto const& itANode : mAnchorNodes) {
		std::string aNodeName = itANode.first;
		std::shared_ptr<Node> aNode = itANode.second;
		if (aNode->vOutEdges.size() == 0) isolatedANodes += 1;
	}
	for (auto const& itRNode : mReadNodes) {
		std::string rNodeName = itRNode.first;
		std::shared_ptr<Node> rNode = itRNode.second;
		if (rNode->vOutEdges.size() == 0) isolatedRNodes += 1;
	}

	bGraphCreated = 1;
  }


  void SBridger::cleanupGraph(void) {
  	// TODO: this is currently a placeholder
  	// Most of the cleanup was already done when constructing the graph
  }

  int SBridger::generatePaths(void) {
  	uint32_t numPaths_maxOvl = scara::generatePathsDeterministic(vPaths, mAnchorNodes, PGT_MAXOS);
  	if (scara::print_output)
  		std::cerr << "\nSCARA: Generating paths using maximum overlap score. Number of paths generated: " << numPaths_maxOvl;
    uint32_t numPaths_maxExt = scara::generatePathsDeterministic(vPaths, mAnchorNodes, PGT_MAXES);
    if (scara::print_output)
    	std::cerr << "\nSCARA: Generating paths using maximum extension score. Number of paths generated: " << numPaths_maxExt;
    uint32_t minMCPaths = numPaths_maxExt + numPaths_maxOvl;
    if (minMCPaths < scara::MinMCPaths) minMCPaths = scara::MinMCPaths;
    uint32_t numPaths_MC = scara::generatePaths_MC(vPaths, mAnchorNodes, minMCPaths);
    if (scara::print_output)
  		std::cerr << "\nSCARA: Generating paths using Monte Carlo approach. Number of paths generated: " << numPaths_MC;


    return vPaths.size();
  }

  int SBridger::groupAndProcessPaths(void) {
  	int numGroups = 0;
  	std::vector<shared_ptr<PathGroup>> tempPathGroups;
  	
  	/* KK: Was only for testing */
  	// Printing paths before processing
  	if (scara::globalDebugLevel >= DL_DEBUG) {
  		std::cerr << "\n\nSCARA: paths before processing:";
  		for (auto const& path_ptr : vPaths) {
  			shared_ptr<PathInfo> pathinfo_ptr = make_shared<PathInfo>(path_ptr);
  			std::cerr << "\nPATHINFO: SNODE(" << pathinfo_ptr->startNodeName << "), ";
  			std::cerr << "ENODE(" << pathinfo_ptr->endNodeName << "), ";
 			std::cerr << "DIRECTION(" << Direction2String(pathinfo_ptr->pathDir) << "), ";
  			std::cerr << "NODES(" << pathinfo_ptr->numNodes << "), ";
  			std::cerr << "BASES(" << pathinfo_ptr->length << "), ";
  			std::cerr << "BASES2(" << pathinfo_ptr->length2 << "), ";
  			std::cerr << "AVG SI(" << pathinfo_ptr->avgSI << "), ";
  			std::cerr << "CONSISTENT(" << checkPath(pathinfo_ptr->path_ptr) << ")";
  		}
  	}
  	/**/

  	// Path extending to the left are reversed so that all paths extend to the right
  	// Simulaneously paths are grouped into buckets of set size
  	if (scara::globalDebugLevel >= DL_VERBOSE) {
  		std::cerr << "\n\nSCARA: paths after processing:";
  	}
  	for (auto const& path_ptr : vPaths) {
  		shared_ptr<Edge> firstEdge = path_ptr->edges[0];
  		Direction dir = D_LEFT;
        if (firstEdge->QES2 > firstEdge->QES1) dir = D_RIGHT;
        shared_ptr<PathInfo> pathinfo_ptr;
        if (dir == D_RIGHT) pathinfo_ptr = make_shared<PathInfo>(path_ptr);
        else {
        	shared_ptr<Path> revPath = path_ptr->reversedPath();
        	pathinfo_ptr = make_shared<PathInfo>(revPath);
        }

  		vPathInfos.emplace_back(pathinfo_ptr);
  		if (scara::globalDebugLevel >= DL_VERBOSE) {
  			std::cerr << "\nPATHINFO: SNODE(" << pathinfo_ptr->startNodeName << "), ";
  			std::cerr << "ENODE(" << pathinfo_ptr->endNodeName << "), ";
 			std::cerr << "DIRECTION(" << Direction2String(pathinfo_ptr->pathDir) << "), ";
  			std::cerr << "NODES(" << pathinfo_ptr->numNodes << "), ";
  			std::cerr << "BASES(" << pathinfo_ptr->length << "), ";
  			std::cerr << "BASES2(" << pathinfo_ptr->length2 << "), ";
  			std::cerr << "AVG SI(" << pathinfo_ptr->avgSI << "), ";
  			std::cerr << "CONSISTENT(" << checkPath(pathinfo_ptr->path_ptr) << ")";
  		}

  		// Grouping the path
  		bool grouped = false;
  		for (auto const& pgroup_ptr : tempPathGroups) {
  			if (pgroup_ptr->addPathInfo(pathinfo_ptr)) {
  				grouped = true;
  				break;
  			}
  		}

  		if (!grouped) {
  			numGroups += 1;
  			shared_ptr<PathGroup> pgroup_ptr = make_shared<PathGroup>(pathinfo_ptr);
  			tempPathGroups.emplace_back(pgroup_ptr);
  		}
  	}

  	if (scara::globalDebugLevel >= DL_INFO) std::cerr << "\n\nSCARA: Discarding groups with less than :" << scara::MinPathsinGroup << " paths!";
  	for (auto const& pgroup_ptr : tempPathGroups) {
  		if (pgroup_ptr->numPaths < scara::MinPathsinGroup) {
  			if (scara::globalDebugLevel >= DL_VERBOSE) {
  				std::cerr << "\nSCARA: Discarding PATHGROUP: SNODE(" << pgroup_ptr->startNodeName << "), ";
	  			std::cerr << "ENODE(" << pgroup_ptr->endNodeName << "), ";
	  			std::cerr << "BASES(" << pgroup_ptr->length << "), ";
	  			std::cerr << "PATHS(" << pgroup_ptr->numPaths << "), ";
  			}
  		}
  		else vPathGroups.emplace_back(pgroup_ptr);
  	}


  	if (scara::globalDebugLevel >= DL_VERBOSE) {
  		std::cerr << "\n\nSCARA: Groups before processing:";
	  	for (auto const& pgroup_ptr : vPathGroups) {
	  		std::cerr << "\nPATHGROUP: SNODE(" << pgroup_ptr->startNodeName << "), ";
  			std::cerr << "ENODE(" << pgroup_ptr->endNodeName << "), ";
  			std::cerr << "BASES(" << pgroup_ptr->length << "), ";
  			std::cerr << "PATHS(" << pgroup_ptr->numPaths << "), ";
	  	}
	}

	// For each node that acts as a starting node preserve only the best group
	// Currently this is a group with the largest number of paths
	// 1. Construct a Map with StartNode name as key and a vector of corresponding groups as value
	std::map<std::string, shared_ptr<vector<shared_ptr<PathGroup>>>> mGroups;
	for (auto const& pgroup_ptr : vPathGroups) {
		if (mGroups.find(pgroup_ptr->startNodeName) == mGroups.end()) {
			shared_ptr<vector<shared_ptr<PathGroup>>> val = make_shared<vector<shared_ptr<PathGroup>>>();
			val->emplace_back(pgroup_ptr);
			mGroups[pgroup_ptr->startNodeName] = val;
		}
		else {
			shared_ptr<vector<shared_ptr<PathGroup>>> val = mGroups[pgroup_ptr->startNodeName];
			val->emplace_back(pgroup_ptr);
			mGroups[pgroup_ptr->startNodeName] = val;		// KK: probably unnecessary
		}
	}

	// 2. For each startNode in the map use only the best PathGroup
	std::vector<shared_ptr<PathGroup>> vFilteredGroups;
	for (auto const& it : mGroups) {
		shared_ptr<vector<shared_ptr<PathGroup>>> val = it.second;
		shared_ptr<PathGroup> bestGroup = val->front();
		double bestSize = bestGroup->numPaths;
		for (uint32_t i=1; i<val->size(); i++) {
			shared_ptr<PathGroup> pgroup_ptr = (*val)[i];
			if (pgroup_ptr->numPaths > bestSize) {
				bestGroup = pgroup_ptr;
				bestSize = pgroup_ptr->numPaths;
			}
		}
		vFilteredGroups.emplace_back(bestGroup);
	}

	/*
	// Join groups that contain the same anchoring node, i.e groups 1->2 and 2->3, should be joined
	// to connect all three anchoring nodes in a single scaffold
	// First find all nodes that are starting nodes for a path, but are not ending nodes of any path
	// Those nodes will start a scaffold
	// TODO: cound have a lot of trouble with circular genome and also if errors cause circular links in a graph
	std::set<std::string> startNodes;
	std::set<std::string> endNodes;
	std::set<std::string> usedNodes; // Each node can only be used once 
	std::vector<shared_ptr<std::vector<shared_ptr<PathGroup>>>> scaffolds_temp;
	std::vector<shared_ptr<std::vector<shared_ptr<PathGroup>>>> scaffolds_filtered;
	for (auto const& pgroup_ptr : vFilteredGroups) {
		startNodes.insert(pgroup_ptr->startNodeName);
		endNodes.insert(pgroup_ptr->endNodeName);
	}

	// Only look at start nodes which are not in the end nodes set!
	for (auto const& startNode : startNodes) {
		if (endNodes.find(startNode) == endNodes.end()) {
			// Create a scaffold (a series of connected paths)
			auto newVec = make_shared<std::vector<shared_ptr<PathGroup>>>();
			// We can assume that the path with the considered startNode exists!
			std::string newStartNode = startNode;
			do {
				auto pgroup_ptr = vFilteredGroups[newStartNode];
				newVec->emplace_back(pgroup_ptr);
				newStartNode = pgroup_ptr->endNodeName;
			} while (vFilteredGroups.find(newStartNode) != vFilteredGroups.end());
			scaffolds_temp.emplace_back(newVec);
		}
	}
	*/

	// Join groups that contain the same anchoring node, i.e groups 1->2 and 2->3, should be joined
	// to connect all three anchoring nodes in a single scaffold
	// 1. sort all groups acoring to the number of paths, descending
	// 2. Keep a record of used nodes
	// 3. Do while changes can be made (the vector is empty)
	//		- start a scaffold with the first group (if possible)
	//			+ remove it from the vector
	//		- traverse all other groups REPEATEDLY and try to extend the current scaffold
	//		- when done, place the scaffold in the scaffold set

	std::vector<shared_ptr<std::vector<shared_ptr<PathGroup>>>> scaffolds_temp;
	std::vector<shared_ptr<std::vector<shared_ptr<PathGroup>>>> scaffolds_filtered;

	// 1. sort (with lambda expressions)
	sort(vFilteredGroups.begin(), vFilteredGroups.end(), [](const auto& lhs, const auto& rhs )
	{
		return lhs->numPaths > rhs->numPaths;
	});
	// 2. record of used nodes
	std::set<std::string> usedNodes;
	// 3. extend scaffolds
	while (vFilteredGroups.size() > 0) {
		// Take the first group and remove it, this will eventually remove all groups from the vector
		shared_ptr<PathGroup> cur_pgroup_ptr = vFilteredGroups.front();
		vFilteredGroups.erase(vFilteredGroups.begin());

		// Check either start or end node is used, skip this group
		if (usedNodes.find(cur_pgroup_ptr->startNodeName) != usedNodes.end() ||
		   (usedNodes.find(cur_pgroup_ptr->endNodeName) != usedNodes.end())) continue;
		
		// Start a new scaffold
		auto newScaff = make_shared<std::vector<shared_ptr<PathGroup>>>();
		newScaff->emplace_back(cur_pgroup_ptr);
		usedNodes.insert(cur_pgroup_ptr->startNodeName);
		usedNodes.insert(cur_pgroup_ptr->endNodeName);

		// Repeatedly check other groups to see if they can extend the scaffold
		bool changes = true;
		while (changes) {
			changes = false;
			// Look at all other groups
			for (auto const& pgroup_ptr : vFilteredGroups) {
				// check if it can continue the scaffold at the back (endNode is not used!)
				if ((pgroup_ptr->startNodeName == (newScaff->back())->endNodeName) &&
					(usedNodes.find(pgroup_ptr->endNodeName) == usedNodes.end())) {
					newScaff->emplace_back(pgroup_ptr);
					usedNodes.insert(pgroup_ptr->endNodeName);
					changes = true;
					continue;		// Probably uncessesary
				}
				// check if it can continue the scaffold at the front (startNode is not used!)
				if ((pgroup_ptr->endNodeName == (newScaff->front())->startNodeName) &&
					(usedNodes.find(pgroup_ptr->startNodeName) == usedNodes.end())) {
					newScaff->emplace(newScaff->begin(), pgroup_ptr);
					usedNodes.insert(pgroup_ptr->startNodeName);
					changes = true;
					continue;		// Probably uncessesary
				}			
			}
		}
		scaffolds_temp.emplace_back(newScaff);

	}

	if (scara::globalDebugLevel >= DL_VERBOSE) {
  		std::cerr << "\n\nSCARA: Initial scaffolds:";
  		int i= 0;
	  	for (auto const& vec_ptr : scaffolds_temp) {
	  		i++;
	  		std::cerr << "\nSCAFFOLD " << i << ": ";
	  		for (auto const& pgroup_ptr : (*vec_ptr)) {
	  			std::cerr << pgroup_ptr->startNodeName << " --> " << pgroup_ptr->endNodeName << "(" << pgroup_ptr->numPaths << "), ";
	  		}
	  	}
	}

	// Eliminating duplicate scaffolds
	// Since two nodes were generated for each contig and read (FW and RC), and two edges for each overlap
	// there should be two identical scaffolds, one on FW and the other on RC strand
	// Drop one of them

	std::cerr << "\n\nSCARA: Eliminating duplicate scaffolds:";
	std::cerr << "\n......";
	for (auto const& vec_ptr : scaffolds_temp) {
		bool found = false;
		for (auto const& vec_ptr2 : scaffolds_filtered) {
			found = scaffoldsEqual(vec_ptr, vec_ptr2);
			if (found) {
				break;
			}
		}
		if (found) {
		// If equivalent scaffold already exists in the list of final scaffolds, jusr skip this one
		// TODO: check which one is better and use that one
		}
		else {
			scaffolds_filtered.emplace_back(vec_ptr);
		}

	}

	if (scara::globalDebugLevel >= DL_VERBOSE) {
  		std::cerr << "\n\nSCARA: Final scaffolds before sequence generation:";
  		int i= 0;
	  	for (auto const& vec_ptr : scaffolds_filtered) {
	  		i++;
	  		std::cerr << "\nSCAFFOLD " << i << ": ";
	  		for (auto const& pgroup_ptr : (*vec_ptr)) {
	  			std::cerr << pgroup_ptr->startNodeName << " --> " << pgroup_ptr->endNodeName << "(" << pgroup_ptr->numPaths << "), ";
	  		}
	  	}
	}

	// Processing scaffolds by chosing a best path within PathGroup
	// scaffolds_temp contains vectors of PathGroups
	// scaffolds contains vectors of PathInfos, pointing to the best path for each group
	for (auto const&  vec_ptr: scaffolds_filtered) {
		auto newVec = make_shared<std::vector<shared_ptr<PathInfo>>>();
		for (auto const& pgroup_ptr : (*vec_ptr)) {
			auto best_pinfo_ptr = pgroup_ptr->vPathInfos[0];
	  		auto best_avgSI = best_pinfo_ptr->avgSI;
	  		for (auto const& pinfo_ptr : pgroup_ptr->vPathInfos) {		// KK: looking at the first element again, lazy to write it better
				if (pinfo_ptr->avgSI > best_avgSI) {
					best_avgSI = pinfo_ptr->avgSI;
					best_pinfo_ptr = pinfo_ptr;
				}
			}
			newVec->emplace_back(best_pinfo_ptr);
		}
		scaffolds.emplace_back(newVec);
	}

  	return scaffolds.size();
  }


  /* Each scaffold is represented as a series of paths, following one another, with the end node of a previous path
   * being the start node of the next one. Start and end nodes represent contigs, and they should be used completely
   * for generating a scaffold sequence, with the holes filled up using reads.
   * 1. Determine scaffold size and allocate enough space for a compelte sequence.
   * 2. Itterate over paths and generate sequence for each path
   *	Take care to use each contig only once (since they are present twice, except for the first and last one)
   */
  int SBridger::generateSequences(void) {
  	int i = 0;
  	std::set<std::string> usedContigs;
  	for (auto const&  vec_ptr: scaffolds) {
  		// bool fistContigUsed = false;
  		i++;
  		// Generate header and calculate scaffold length
  		std::string header = ">Scaffold_" + to_string(i);
  		if (scara::globalDebugLevel >= DL_INFO) {
	  		cerr << "\nSCARA: Generating sequence and header for scaffold " << i << endl;
	  		cerr << "SCARA: scaffold edges: ";
  		}

  		uint32_t slength = 0;
		uint32_t lastNodeLength = 0;
		uint32_t numNodes = 1;
  		for (auto const& pinfo_ptr : (*vec_ptr)) {
  			header += ' ' + pinfo_ptr->path_ptr->edges.front()->getStartNodeName();
  			slength += pinfo_ptr->length;
  			// Remove the length of the endNode (as not to be added twice)
  			auto lastEdge = pinfo_ptr->path_ptr->edges.back();
  			lastNodeLength = lastEdge->ELen;
  			slength -= lastNodeLength;
  			numNodes += pinfo_ptr->path_ptr->edges.size();

  			cerr << pinfo_ptr->path_ptr->edges.size() << " (" << pinfo_ptr->length << ") ";
  		}
  		header += ' ' + vec_ptr->back()->path_ptr->edges.back()->getEndNodeName();		// Add the last endNode
  		slength += lastNodeLength;		// For the last path, add the endNode length

  		// Output the scaffold header to cout!
  		if (scara::globalDebugLevel >= DL_INFO) cerr << "\nSCARA generated header " << header << endl;
  		cout << header << endl;

  		if (scara::globalDebugLevel >= DL_INFO) 
  			cerr << "SCARA generating sequence of length " << slength << " from " << numNodes << " nodes!" << endl;

  		// Calculate scaffold sequence from scaffoldPath and output it to cout
  		// NOTE: Assuming direction RIGHT!
  		std::shared_ptr<Node> lastEndNode = NULL;
		for (auto const& pinfo_ptr : (*vec_ptr)) {
			std::string startNodeName = pinfo_ptr->startNodeName;
			std::string endNodeName = pinfo_ptr->endNodeName;
			usedContigs.emplace(startNodeName);
			usedContigs.emplace(getRCNodeName(startNodeName));
			usedContigs.emplace(endNodeName);
			usedContigs.emplace(getRCNodeName(endNodeName));
			// TODO: Check if any of the contigs were used more than once
			for (auto const& edge_ptr: pinfo_ptr->path_ptr->edges) {
	  			// Determine part of the startNode that will be put into the final sequence
	  			shared_ptr<Node> startNode = edge_ptr->startNode;
	  			uint32_t seq_part_start, seq_part_end, seq_part_size;
	  			std::string seq_part = "";
	  			
  				seq_part_start = 0;
  				seq_part_end = edge_ptr->SStart - edge_ptr->EStart;	  			
	  			seq_part_size = seq_part_end - seq_part_start;
	  			if (seq_part_size <= 0) {
	  				throw std::runtime_error(std::string("SCARA BRIDGER: ERROR - invalid sequence part size: "));
	  			}
	  			seq_part.reserve(seq_part_size + 10);		// adding 10 just to avoid missing something by 1

	  			// localStrand = strand;
	  			if (!(startNode->isReverseComplement)) {
	  				// Copy relevant part of the string
	  				uint32_t k=0;
	  				for (; k<seq_part_size; k++) {
	  					seq_part += (startNode->seq_ptr->seq_strData)[seq_part_start+k];
	  				}
	  				// seq_part[k] = '\0';
	  			} else {
	  				// If the strand is reverse, go from the end of the string and rev
	  				uint32_t k=0;
	  				for (; k<seq_part_size; k++) {
	  					uint32_t seq_end = (startNode->seq_ptr->seq_strData).length();
	  					seq_part += _bioBaseComplement((startNode->seq_ptr->seq_strData)[seq_end-seq_part_start-k-1]);
	  				}
	  				// seq_part[k] = '\0';
	  			}

	  			// Output the sequence part to the standard output
	  			if (scara::globalDebugLevel >= DL_DEBUG) {
		  			cerr << "SCARA BRIDGER: Printing node " << startNode->nName << " with length " << seq_part_size << " - ";
		  			cerr << seq_part.length() << "/" << startNode->seq_ptr->seq_strData.length();
		  			cerr << endl;
	  			}
	  			cout << seq_part;

	  			// Setting the endNode of the previous path for the next iteration
	  			lastEndNode = edge_ptr->endNode;
	  		}
  		}

  		// auto lastEndNode = scaffoldPath.edges.back()->endNode;
  		if (scara::globalDebugLevel >= DL_DEBUG) {
  			cerr << "SCARA BRIDGER: Printing node " << lastEndNode->nName << " with length ";
  			cerr << lastEndNode->seq_ptr->seq_strData.length() << "/" << lastEndNode->seq_ptr->seq_strData.length();
  			cerr << endl;
  		}
  		
  		if (!(lastEndNode->isReverseComplement)) {
  			cout << lastEndNode->seq_ptr->seq_strData << endl;
  		} else {
  			cout << _bioReverseComplement(lastEndNode->seq_ptr->seq_strData) << endl;
  		} 
  	}

  	if (scara::globalDebugLevel >= DL_INFO) {
  		cerr << "\nSCARA BRIDGER: Used contigs: ";
	  	for (auto const& usedContig : usedContigs) {
	  		cerr << usedContig + ", ";
	  	}
  		cerr << endl;
  	}

  	cerr << "SCARA BRIDGER: Printing sequences for unsued contigs! There are " << (mAnchorNodes.size() - usedContigs.size());
  	cerr << " potentially unused contigs!" << endl;
	for (auto const& aNodePair : mAnchorNodes) {
		auto aNode = aNodePair.second;
		std::string nodeNameOG = getOGNodeName(aNode->nName);
		// Print only original unused contigs, and not RC ones that were generated 
		if ((aNode->nName == nodeNameOG) && (usedContigs.find(aNode->nName) == usedContigs.end())) {
			cout << ">" << aNode->nName << endl;
			cout << aNode->seq_ptr->seq_strData << endl;
		}
	}


   	return scaffolds.size();
  }

  void SBridger::printOvlToStream(VecOvl &vOvl, ofstream &outStream) {

	for (auto const& it : vOvl) {
		outStream << "(" << it->ext_strTarget << "," << it->ext_strName << ") ";
		outStream << endl;
	}
  }
  

  void SBridger::printNodeToStream(MapIdToNode &map, ofstream &outStream) {
  	for (auto const& it : map) {
  		auto node_ptr = it.second;
  		if (node_ptr->vOutEdges.size() == 0) continue;		// Do not print nodes without outgoing edges
  		outStream << "Edges for node " << it.first << ":" << endl;
  		for (auto const& edge_ptr : node_ptr->vOutEdges) {
  			outStream << "(" << edge_ptr->getStartNodeName() << "," << edge_ptr->getEndNodeName() << ") " << endl;
  		}
  		outStream << endl;
  	}
  }

  // Check if two scaffolds are equivalent
  // First scaffold is checked from the start to the end
  // Second scaffold is checked from the end to the start
  // Node names between scaffolds must be reverse complements
  bool SBridger::scaffoldsEqual(shared_ptr<std::vector<shared_ptr<PathGroup>>> scaff1, shared_ptr<std::vector<shared_ptr<PathGroup>>> scaff2) {
  	if (scaff1->size() != scaff2->size()) return false;
	// KK: This should never happen!
  	if (scaff1->size() == 0 && scaff2->size() == 0) {
  		throw std::runtime_error(std::string("SCARA BRIDGER: ERROR - Scaffolds of size 0"));
  	}
  	uint32_t size = scaff1->size();
  	for (uint32_t i=0; i<size; i++) {
  		std::string sname1 = (*scaff1)[i]->startNodeName;
  		std::string ename1 = (*scaff1)[i]->endNodeName;
  		std::string sname2 = (*scaff2)[size-1-i]->startNodeName;
  		std::string ename2 = (*scaff2)[size-1-i]->endNodeName;

  		// std::cerr << "\nSCARA Test: i=" << i << ": " << sname1 << " and " << ename2 << ", " << sname2 << " and " << sname2;

  		if ((sname1 != getRCNodeName(ename2)) || (ename1 != getRCNodeName(sname2))) return false;
  	}
  	return true;
  }

  // OBSOLETE
  void SBridger::Execute(void) {
  }

}
