#include "CPFMatchingExpGPU.h"

#include "ResourceManager.h"

using namespace texpert;

#define M_PI 3.14159265359

CPFMatchingExpGPU::CPFMatchingExpGPU()
{
	m_verbose = false;
	m_render_helpers = true;
	m_verbose_level = 0;
	m_multiplier = 10.0;

	m_max_points = 0;

	float angle_step_rad = m_params.angle_step / 180.0f * static_cast<float>(M_PI);
	m_angle_bins = (int)(static_cast<float>(2 * M_PI) / angle_step_rad) + 1;


	m_knn = new KNN();
}

CPFMatchingExpGPU::~CPFMatchingExpGPU()
{
	CPFToolsGPU::DeallocateMemory();
}


/*
Set the reference point set.
*/
int CPFMatchingExpGPU::addModel(PointCloud& points, std::string label)
{
	if (points.size() == 0) return -1;

	if (points.points.size() != points.normals.size()) {
		std::cout << "[ERROR] - CPFMatchingExpGPU: points size " << points.points.size() << " != " << points.normals.size() << " for " << label << std::endl;
		return -1;
	}

	m_ref.push_back(points);
	m_ref_labels.push_back(label);

	if (m_verbose) {
		std::cout << "[INFO] - CPFMatchingExpGPU: start extracting descriptors from " << label << " with " << points.size() << " points." << std::endl;
	}

	if (points.size() > m_max_points)
	{
		if (m_max_points != 0)
			CPFToolsGPU::DeallocateMemory();

		CPFToolsGPU::AllocateMemory(points.size());

		m_max_points = points.size();
	}

	//--------------------------------------------------------
	// Start calculating descriptors

	std::vector<CPFDiscreet> descriptors;
	std::vector<uint32_t> curvatures;

	calculateDescriptors(points, m_params.search_radius, descriptors, curvatures);

	m_model_descriptors.push_back(descriptors);
	m_model_curvatures.push_back(curvatures);

	// create an empty data template. 
	m_matching_results.push_back(CPFMatchingData());

	if (m_verbose) {
		std::cout << "[INFO] - CPFMatchingExpGPU: finished extraction of " << descriptors.size() << " descriptors for  " << label << "." << std::endl;
	}

	return m_model_descriptors.size() - 1;
}


bool CPFMatchingExpGPU::setScene(PointCloud& points)
{
	if (points.size() == 0) return false;

	if (points.points.size() != points.normals.size()) {
		std::cout << "[ERROR] - CPFMatchingExpGPU: scene point size != normals size: " << points.points.size() << " != " << points.normals.size() << "." << std::endl;
	}

	m_scene = points;

	if (m_verbose && m_verbose_level == 2) {
		std::cout << "[INFO] - CPFMatchingExpGPU: start extracting scene descriptors for " << points.size() << " points." << std::endl;
	}

	if (points.size() > m_max_points)
	{
		if(m_max_points != 0)
			CPFToolsGPU::DeallocateMemory();
		CPFToolsGPU::AllocateMemory(points.size());

		m_max_points = points.size();
	}

	//--------------------------------------------------------
	// Start calculating descriptors

	calculateDescriptors(points, m_params.search_radius, m_scene_descriptors, m_scene_curvatures);

	if (m_verbose && m_verbose_level == 2) {
		std::cout << "[INFO] - CPFMatchingExpGPU: finished extraction of " << m_scene_descriptors.size() << " scene descriptors for." << std::endl;
	}

	return true;
}

/*
Detect all reference point in the camera point set.
*/
bool CPFMatchingExpGPU::match(int model_id)
{
	if (model_id < 0 || model_id >= m_ref.size()) {
		std::cout << "[ERROR] - Selected model id " << model_id << " for matching does not exist.";
		return false;
	}
	if (m_scene.size() <= 0) {
		std::cout << "[ERROR] - No scene set. Set a scene model first." << std::endl;
		return false;
	}

	// initialize the render helpers. 
	if (m_render_helpers) {
		m_helpers.init(m_ref[model_id].size(), m_scene.size());
	}

	// matching and voting
	matchDescriptors(m_model_descriptors[model_id], m_scene_descriptors, m_ref[model_id], m_scene, m_matching_results[model_id]);

	// cluster the poses
	bool ret = clustering(m_matching_results[model_id]);


	// get the siz best hits
	int hits = 12;
	if (m_matching_results[model_id].pose_clusters.size() < hits);
	hits = m_matching_results[model_id].pose_clusters.size() - 1;


	for (int i = 0; i < hits; i++) {
		// <votes, cluster id> in pose_cluster
		std::pair< int, int>  votes_cluster_index = m_matching_results[model_id].pose_cluster_votes[i];

		combinePoseCluster(m_matching_results[model_id].pose_clusters[votes_cluster_index.second], votes_cluster_index.first, m_matching_results[model_id], false);

		//m_matching_results[model_id].poses.push_back(  m_matching_results[model_id].pose_clusters[votes_cluster_index.second].front() ); // get the first matrix. 
		//m_matching_results[model_id].poses_votes.push_back( votes_cluster_index.first);
	}

	return ret;
}

/*
*/
bool CPFMatchingExpGPU::setVerbose(bool verbose, int level)
{
	m_verbose_level = std::min(2, std::max(0, level));
	m_verbose = verbose;
	return m_verbose;
}

/*
Set the parameters for the extraction tool.
*/
bool CPFMatchingExpGPU::setParams(CPFParams params)
{
	m_params.angle_step = std::max(1.0f, std::min(180.0f, params.angle_step));
	m_params.search_radius = std::max(0.001f, std::min(10.0f, params.search_radius));
	m_params.cluster_trans_threshold = std::max(0.0001f, std::min(100.0f, params.cluster_trans_threshold));
	m_params.cluster_rot_threshold = std::max(0.0f, std::min(180.0f, params.cluster_rot_threshold)) / 180.0f * static_cast<float>(M_PI);  // to rad
	m_multiplier = std::max(1.0f, std::min(100.0f, params.multiplier));

	float angle_step_rad = m_params.angle_step / 180.0f * static_cast<float>(M_PI); // to rad
	m_angle_bins = (int)(static_cast<float>(2 * M_PI) / angle_step_rad) + 1;

	return true;
}



// Descriptor based on curvature pairs and the direction vector
void CPFMatchingExpGPU::calculateDescriptors(PointCloud& pc, float radius, std::vector<CPFDiscreet>& descriptors, std::vector<uint32_t>& curvatures)
{
	CPFToolsGPU::CPFParamGPU param;
	param.angle_bins = m_angle_bins;
	CPFToolsGPU::SetParam(param);

	//----------------------------------------------------------------------------------------------------------
	// nearest neighbors

	std::vector<Matches> matches;
	size_t s = pc.size();


	m_knn->populate(pc);

	matches.clear();

	m_knn->radius(pc, radius, matches);

	CPFToolsGPU::AssignPointCloud(pc);
	//CPFToolsGPU::AssignMatches(matches);

	//----------------------------------------------------------------------------------------------------------
	// Calculate point curvatures

	curvatures.clear();
	curvatures.reserve(s);

	CPFToolsGPU::DiscretizeCurvature(curvatures, pc.normals, pc, matches, m_multiplier);

	descriptors.clear();
	descriptors.reserve(s);

	//----------------------------------------------------------------------------------------------------------
	// Calculate the descriptor
	std::vector<Eigen::Affine3f> Ts;
	CPFToolsGPU::GetRefFrames(Ts, pc.points, pc.normals);
	CPFToolsGPU::DiscretizeCPF(descriptors, curvatures, matches, pc.points, Ts);
}


/*
Match the model and scene descriptors.
*/
void CPFMatchingExpGPU::matchDescriptors(std::vector<CPFDiscreet>& src_model, std::vector<CPFDiscreet>& src_scene, PointCloud& pc_model, PointCloud& pc_scene, CPFMatchingData& dst_data)

{
	if (m_verbose && m_verbose_level == 2) {
		std::cout << "[INFO] - CPFMatchingExpGPU: Start matching descriptors." << std::endl;
	}

	int scr_s_size = src_scene.size();
	int src_m_size = src_model.size();

	int model_point_size = pc_model.size();
	int scene_point_size = pc_scene.size();

	dst_data.voting_clear();

	for (int i = 0; i < model_point_size; i++) {

		int point_id = i;

		std::vector<int> accumulator(scene_point_size * m_angle_bins, 0);

		int count = 0;

		// -------------------------------------------------------------------
		// For each point i and its descriptors, find matching descriptors.
		for (int k = 0; k < src_m_size; k++) {

			CPFDiscreet src = src_model[k];

			if (src.point_idx != point_id || src.data[0] == 0) continue; // must be a descriptor for the current point i

			// search for the destination descriptor
			for (int j = 0; j < scr_s_size; j++) {

				CPFDiscreet dst = src_scene[j];
				if (dst.data[0] == 0) continue;

				// compare the descriptor
				if (src.data[0] == dst.data[0] && src.data[1] == dst.data[1] && src.data[2] == dst.data[2] && dst.data[0] != 0) {

					// Voting, fill the accumulator
					float alpha = src.alpha - dst.alpha;

					int alpha_bin = static_cast<int>(static_cast<float>(m_angle_bins) * ((alpha + 2.0f * static_cast<float>(M_PI)) / (4.0f * static_cast<float>(M_PI))));
					alpha_bin = std::max(0, std::min(alpha_bin, m_angle_bins));


					accumulator[dst.point_idx * m_angle_bins + alpha_bin]++;

					// store the output vote pair
					dst_data.vote_pair.push_back(make_pair(i, alpha));


					// render helpers store the matches
					if (m_render_helpers) {
						m_helpers.addMatchingPair(point_id, dst.point_idx);
					}
					//pair_ids.push_back(make_pair(i, dst.point_idx));

				}
			}

		}

		// -------------------------------------------------------------------
		// Find the voting winner

		int max_vote = 0;
		vector<int> max_votes_idx;
		vector<int> max_votes_value;

		for (int k = 0; k < accumulator.size(); k++) {
			if (accumulator[k] >= max_vote && accumulator[k] != 0) {
				max_vote = accumulator[k];
				max_votes_idx.push_back(k);
				max_votes_value.push_back(accumulator[k]);
			}
			accumulator[k] = 0; // Set it to zero for next iteration
		}

		// -----------------------------------------------------------------------
		// Recover the pose

		for (int k = 0; k < max_votes_idx.size(); k++) {
			if (max_vote == max_votes_value[k]) {

				int max_scene_id = max_votes_idx[k] / m_angle_bins; // model id
				int max_alpha = max_votes_idx[k] % m_angle_bins; // restores the angle


				Eigen::Vector3f model_point(pc_model.points[point_id][0], pc_model.points[point_id][1], pc_model.points[point_id][2]);
				Eigen::Vector3f model_normal(pc_model.normals[point_id].x(), pc_model.normals[point_id].y(), pc_model.normals[point_id].z());

				Eigen::Vector3f scene_point(pc_scene.points[max_scene_id][0], pc_scene.points[max_scene_id][1], pc_scene.points[max_scene_id][2]);
				Eigen::Vector3f scene_normal(pc_scene.normals[max_scene_id].x(), pc_scene.normals[max_scene_id].y(), pc_scene.normals[max_scene_id].z());

				Eigen::Affine3f T = CPFTools::GetRefFrame(model_point, model_normal);
				Eigen::Affine3f Tmg = CPFTools::GetRefFrame(scene_point, scene_normal);

				float angle = (static_cast<float>(max_alpha) / static_cast<float>(m_angle_bins)) * 4.0f * static_cast<float>(M_PI) - 2.0f * static_cast<float>(M_PI);

				Eigen::AngleAxisf rot(angle, Eigen::Vector3f::UnitX());

				// Compose the transformations for the final pose
				Eigen::Affine3f final_transformation(Tmg.inverse() * rot * T);


				// RENDER HELPER
				//vote_ids.push_back(make_pair(point_id, max_model_id));
				if (m_render_helpers) {
					m_helpers.addVotePair(point_id, max_scene_id);
				}

				dst_data.pose_candidates.push_back(final_transformation);
				dst_data.pose_candidates_votes.push_back(max_votes_value[k]);

				//std::cout << "\tangle: " << angle << std::endl;
			}
		}

	}

	if (m_verbose && m_verbose_level == 2) {
		std::cout << "[INFO] - CPFMatchingExpGPU: Found " << dst_data.pose_candidates.size() << " pose candidates." << std::endl;
	}


}



bool CPFMatchingExpGPU::clustering(CPFMatchingData& data)
{

	if (data.pose_candidates.size() == 0) {
		if (m_verbose) {
			std::cout << "[INFO] - CPFMatchingExpGPU: Found " << data.pose_candidates.size() << " pose candidates." << std::endl;
		}
		return false;
	}

	data.cluster_clear();
	int cluster_count = 0;

	for (int i = 0; i < data.pose_candidates.size(); i++) {

		bool cluster_found = false;

		Eigen::Affine3f pose = data.pose_candidates[i];

		// check whether the new pose fits into an existing cluster
		for (int j = 0; j < data.pose_clusters.size(); j++) {

			if (similarPose(pose, data.pose_clusters[j].front())) {
				cluster_found = true;
				data.pose_clusters[j].push_back(pose); // remember the cluster
				data.pose_cluster_votes[j].first += data.pose_candidates_votes[i]; // count the votes

				// RENDER HELPER
				if (m_render_helpers) {
					data.debug_pose_candidates_id[j].push_back(i); // for debugging. Store the pose candiate id. 
				}
			}

		}

		// create a new cluster 
		if (!cluster_found) {
			std::vector<Eigen::Affine3f> cluster;
			cluster.push_back(pose);
			data.pose_clusters.push_back(cluster); // remember the cluster
			data.pose_cluster_votes.push_back(make_pair(data.pose_candidates_votes[i], cluster_count)); // count the votes
			cluster_count++;

			// RENDER HELPER
			// to render the lines appropriate
			if (m_render_helpers) {

				std::vector<int> debug_id;
				debug_id.push_back(i);
				data.debug_pose_candidates_id.push_back(debug_id); // for debugging. Store the pose candiate id. 
			}
		}



	}

	// sort the cluster
	std::sort(data.pose_cluster_votes.begin(), data.pose_cluster_votes.end(),
		[](const std::pair<int, int>& a, const std::pair<int, int>& b) {
			return a.first > b.first; });


	if (m_verbose) {
		std::cout << "[INFO] - CPFMatchingExpGPU: Found " << data.pose_clusters.size() << " pose clusters." << std::endl;
	}

	return true;
}


bool CPFMatchingExpGPU::similarPose(Eigen::Affine3f a, Eigen::Affine3f b)
{
	float translation_threshold = 0.03;
	float rotation_threshold = 0.8;

	// distance between the two positions. 
	float delta_t = (a.translation() - b.translation()).norm();

	// angle delta
	Eigen::AngleAxisf aa(a.rotation().inverse() * b.rotation());
	float delta_r = fabsf(aa.angle());

	return delta_t < m_params.cluster_trans_threshold;// && delta_r < m_params.cluster_rot_threshold;
}


/*
Return the poses for a particular model;
*/
bool  CPFMatchingExpGPU::getPose(const int model_id, std::vector<Eigen::Affine3f >& poses, std::vector<int >& pose_votes)
{
	if (model_id < 0 || model_id >= m_ref.size()) {
		std::cout << "[ERROR] - Selected model id " << model_id << " for matching does not exist.";
		return false;
	}

	poses = m_matching_results[model_id].poses;
	pose_votes = m_matching_results[model_id].poses_votes;

	return true;
}


bool CPFMatchingExpGPU::combinePoseCluster(std::vector<Eigen::Affine3f>& pose_clustser, int votes, CPFMatchingData& dst_data, bool invert = false)
{
	if (pose_clustser.size() == 0) {
		if (m_verbose) {
			std::cout << "[ERROR] - CPFMatchingExpGPU:combinePoseCluster: No pose clusters give ( " << pose_clustser.size() << " )." << std::endl;
		}
		return false;
	}

	Eigen::Vector4f rot_avg(0.0, 0.0, 0.0, 0.0);
	Eigen::Vector3f trans_avg(0.0, 0.0, 0.0);

	// get an avarage pose for the cluster with the hightest number of votes.
	for_each(pose_clustser.begin(), pose_clustser.end(),
		[&](const Eigen::Affine3f& p) {
			trans_avg += p.translation();
			rot_avg += Eigen::Quaternionf(p.rotation()).coeffs();
		});

	size_t size = pose_clustser.size();

	trans_avg /= (float)size;
	rot_avg /= (float)size;

	Eigen::Affine3f pose;
	pose = Eigen::Affine3f::Identity();
	pose.linear().matrix() = Eigen::Quaternionf(rot_avg).normalized().toRotationMatrix();
	pose.translation().matrix() = trans_avg;


	// Inverse to move the model into the scene point cloud. 
	if (invert)
		pose = pose.inverse();

	dst_data.poses.push_back(pose);
	dst_data.poses_votes.push_back(votes);

	return true;
}


/*
Enable or disable render helpers that
allow one to debug the code.
*/
bool CPFMatchingExpGPU::enableRenderHelpers(bool enable)
{
	m_render_helpers = enable;
	return m_render_helpers;
}


CPFRenderHelpers& CPFMatchingExpGPU::getRenderHelper(void)
{
	return m_helpers;
}


bool CPFMatchingExpGPU::getPoseClusterPairs(const int cluster_id, std::vector< std::pair<int, int> >& cluster_point_pairs)
{
	cluster_point_pairs.clear();

	int model_id = 0;

	if (cluster_id >= m_matching_results[model_id].pose_cluster_votes.size()) return false;


	// votes, cluster id
	std::pair< int, int>  votes_cluster_index = m_matching_results[model_id].pose_cluster_votes[cluster_id];


	std::cout << "[INFO] - Rendering cluster id " << cluster_id << " with " << votes_cluster_index.first << " votes." << endl;

	std::vector<int> pose_candidate_ids = m_matching_results[model_id].debug_pose_candidates_id[votes_cluster_index.second];


	for (int i = 0; i < pose_candidate_ids.size(); i++) {

		cluster_point_pairs.push_back(m_helpers.getVotePair(pose_candidate_ids[i]));
	}

	return true;
}


int CPFMatchingExpGPU::getNumPoseClusters(int model_id)
{
	if (model_id >= m_matching_results.size())
		return -1;

	return m_matching_results[model_id].pose_clusters.size();
}


bool CPFMatchingExpGPU::getModelCurvature(const int model_id, std::vector<uint32_t>& model_cu)
{
	model_cu = m_model_curvatures[model_id];
	return true;
}


bool CPFMatchingExpGPU::getSceneCurvature(std::vector<uint32_t>& scene_cu)
{
	scene_cu = m_scene_curvatures;
	return true;
}