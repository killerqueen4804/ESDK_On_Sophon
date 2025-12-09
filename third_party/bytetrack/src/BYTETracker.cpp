//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "BYTETracker.h"
#include <iostream>
#include <algorithm>

namespace bytetrack {

BYTETracker::BYTETracker(const ByteTrackParams& params) {
    track_thresh_ = params.track_thresh;
    match_thresh_ = params.match_thresh;
    frame_rate_ = params.frame_rate;
    track_buffer_ = params.track_buffer;
    min_box_area_ = params.min_box_area;
    frame_id_ = 0;
    max_time_lost_ = static_cast<int>(frame_rate_ / 30.0 * track_buffer_);
    kalman_filter_ = std::make_shared<KalmanFilter>();
}

BYTETracker::~BYTETracker() {}

void BYTETracker::update(STracks& output_stracks, 
                         const std::vector<ByteTrackBox>& objects) {
    ////////////////// Step 1: Get detections //////////////////
    frame_id_++;
    STracks activated_stracks;
    STracks refind_stracks;
    STracks detections;
    STracks detections_low;
    STracks detections_cp;
    STracks tracked_stracks_swap;
    STracks resa, resb;
    STracks temp_tracked_stracks;
    STracks temp_lost_stracks;
    STracks temp_removed_stracks;
    STracks unconfirmed;
    STracks strack_pool;
    STracks r_tracked_stracks;

    if (!objects.empty()) {
        for (size_t i = 0; i < objects.size(); i++) {
            std::vector<float> tlbr(4);
            tlbr[0] = objects[i].x;
            tlbr[1] = objects[i].y;
            tlbr[2] = objects[i].x + objects[i].width;
            tlbr[3] = objects[i].y + objects[i].height;

            float score = objects[i].score;
            int class_id = objects[i].class_id;

            auto strack = std::make_shared<STrack>(
                STrack::tlbr_to_tlwh(tlbr), score, class_id);
            
            if (score >= track_thresh_) {
                detections.push_back(strack);
            } else {
                detections_low.push_back(strack);
            }
        }
    }

    // Add newly detected tracklets to tracked_stracks
    for (size_t i = 0; i < tracked_stracks_.size(); i++) {
        if (!tracked_stracks_[i]->is_activated)
            unconfirmed.push_back(tracked_stracks_[i]);
        else
            temp_tracked_stracks.push_back(tracked_stracks_[i]);
    }

    ////////////////// Step 2: First association, with IoU //////////////////
    joint_stracks(temp_tracked_stracks, lost_stracks_, strack_pool);
    STrack::multi_predict(strack_pool, kalman_filter_);

    std::vector<std::vector<float>> dists;
    int dist_size = static_cast<int>(strack_pool.size());
    int dist_size_size = static_cast<int>(detections.size());
    iou_distance(strack_pool, detections, dists);

    std::vector<std::vector<int>> matches;
    std::vector<int> u_track, u_detection;
    linear_assignment(dists, dist_size, dist_size_size, match_thresh_, matches,
                      u_track, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        auto track = strack_pool[matches[i][0]];
        auto det = detections[matches[i][1]];
        if (track->state == TrackState::Tracked) {
            track->update(kalman_filter_, det, frame_id_);
            activated_stracks.push_back(track);
        } else {
            track->re_activate(kalman_filter_, det, frame_id_, false);
            refind_stracks.push_back(track);
        }
    }

    ////////////////// Step 3: Second association, using low score dets //////////////////
    for (size_t i = 0; i < u_detection.size(); i++) {
        detections_cp.push_back(detections[u_detection[i]]);
    }
    detections.clear();
    detections.assign(detections_low.begin(), detections_low.end());

    for (size_t i = 0; i < u_track.size(); i++) {
        if (strack_pool[u_track[i]]->state == TrackState::Tracked) {
            r_tracked_stracks.push_back(strack_pool[u_track[i]]);
        }
    }

    dists.clear();
    iou_distance(r_tracked_stracks, detections, dists);
    dist_size = static_cast<int>(r_tracked_stracks.size());
    dist_size_size = static_cast<int>(detections.size());

    matches.clear();
    u_track.clear();
    u_detection.clear();
    linear_assignment(dists, dist_size, dist_size_size, 0.5f, matches, 
                      u_track, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        auto track = r_tracked_stracks[matches[i][0]];
        auto det = detections[matches[i][1]];
        if (track->state == TrackState::Tracked) {
            track->update(kalman_filter_, det, frame_id_);
            activated_stracks.push_back(track);
        } else {
            track->re_activate(kalman_filter_, det, frame_id_, false);
            refind_stracks.push_back(track);
        }
    }

    for (size_t i = 0; i < u_track.size(); i++) {
        auto track = r_tracked_stracks[u_track[i]];
        if (track->state != TrackState::Lost) {
            track->mark_lost();
            temp_lost_stracks.push_back(track);
        }
    }

    // Deal with unconfirmed tracks
    detections.clear();
    detections.assign(detections_cp.begin(), detections_cp.end());

    dists.clear();
    iou_distance(unconfirmed, detections, dists);
    dist_size = static_cast<int>(unconfirmed.size());
    dist_size_size = static_cast<int>(detections.size());

    matches.clear();
    std::vector<int> u_unconfirmed;
    u_detection.clear();
    linear_assignment(dists, dist_size, dist_size_size, 0.7f, matches,
                      u_unconfirmed, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        unconfirmed[matches[i][0]]->update(
            kalman_filter_, detections[matches[i][1]], frame_id_);
        activated_stracks.push_back(unconfirmed[matches[i][0]]);
    }

    for (size_t i = 0; i < u_unconfirmed.size(); i++) {
        auto track = unconfirmed[u_unconfirmed[i]];
        track->mark_removed();
        temp_removed_stracks.push_back(track);
    }

    ////////////////// Step 4: Init new stracks //////////////////
    for (size_t i = 0; i < u_detection.size(); i++) {
        auto track = detections[u_detection[i]];
        if (track->score < track_thresh_) continue;
        track->activate(kalman_filter_, frame_id_);
        activated_stracks.push_back(track);
    }

    ////////////////// Step 5: Update state //////////////////
    for (size_t i = 0; i < lost_stracks_.size(); i++) {
        if (frame_id_ - lost_stracks_[i]->end_frame() > max_time_lost_) {
            lost_stracks_[i]->mark_removed();
            temp_removed_stracks.push_back(lost_stracks_[i]);
        }
    }

    for (size_t i = 0; i < tracked_stracks_.size(); i++) {
        if (tracked_stracks_[i]->state == TrackState::Tracked) {
            tracked_stracks_swap.push_back(tracked_stracks_[i]);
        }
    }
    tracked_stracks_.clear();
    tracked_stracks_.assign(tracked_stracks_swap.begin(), tracked_stracks_swap.end());

    joint_stracks(tracked_stracks_, activated_stracks, tracked_stracks_);
    joint_stracks(tracked_stracks_, refind_stracks, tracked_stracks_);

    sub_stracks(lost_stracks_, tracked_stracks_);
    for (size_t i = 0; i < temp_lost_stracks.size(); i++) {
        lost_stracks_.push_back(temp_lost_stracks[i]);
    }

    sub_stracks(lost_stracks_, removed_stracks_);
    for (size_t i = 0; i < temp_removed_stracks.size(); i++) {
        removed_stracks_.push_back(temp_removed_stracks[i]);
    }

    remove_duplicate_stracks(resa, resb, tracked_stracks_, lost_stracks_);

    tracked_stracks_.clear();
    tracked_stracks_.assign(resa.begin(), resa.end());
    lost_stracks_.clear();
    lost_stracks_.assign(resb.begin(), resb.end());

    // Output
    for (size_t i = 0; i < tracked_stracks_.size(); i++) {
        if (tracked_stracks_[i]->is_activated &&
            tracked_stracks_[i]->tlwh[2] * tracked_stracks_[i]->tlwh[3] > min_box_area_) {
            output_stracks.push_back(tracked_stracks_[i]);
        }
    }
}

void BYTETracker::joint_stracks(STracks& tlista, STracks& tlistb, STracks& results) {
    std::map<int, int> exists;
    for (size_t i = 0; i < results.size(); i++)
        exists.insert(std::pair<int, int>(results[i]->track_id, 1));

    for (size_t i = 0; i < tlista.size(); i++) {
        int tid = tlista[i]->track_id;
        if (!exists[tid] || exists.count(tid) == 0) {
            exists[tid] = 1;
            results.push_back(tlista[i]);
        }
    }
    for (size_t i = 0; i < tlistb.size(); i++) {
        int tid = tlistb[i]->track_id;
        if (!exists[tid] || exists.count(tid) == 0) {
            exists[tid] = 1;
            results.push_back(tlistb[i]);
        }
    }
}

void BYTETracker::sub_stracks(STracks& tlista, STracks& tlistb) {
    std::map<int, std::shared_ptr<STrack>> stracks;
    for (size_t i = 0; i < tlista.size(); i++)
        stracks.insert(std::pair<int, std::shared_ptr<STrack>>(
            tlista[i]->track_id, tlista[i]));
    
    for (size_t i = 0; i < tlistb.size(); i++) {
        int tid = tlistb[i]->track_id;
        if (stracks.count(tid) != 0) 
            stracks.erase(tid);
    }
    
    tlista.clear();
    for (auto it = stracks.begin(); it != stracks.end(); ++it)
        tlista.push_back(it->second);
}

void BYTETracker::remove_duplicate_stracks(STracks& resa, STracks& resb,
                                           STracks& stracksa, STracks& stracksb) {
    std::vector<std::vector<float>> pdist;
    iou_distance(stracksa, stracksb, pdist);
    
    std::vector<std::pair<int, int>> pairs;
    for (size_t i = 0; i < pdist.size(); i++) {
        for (size_t j = 0; j < pdist[i].size(); j++) {
            if (pdist[i][j] < 0.15f) {
                pairs.push_back(std::pair<int, int>(static_cast<int>(i), 
                                                     static_cast<int>(j)));
            }
        }
    }

    std::vector<int> dupa, dupb;
    for (size_t i = 0; i < pairs.size(); i++) {
        int timep = stracksa[pairs[i].first]->frame_id -
                    stracksa[pairs[i].first]->start_frame;
        int timeq = stracksb[pairs[i].second]->frame_id -
                    stracksb[pairs[i].second]->start_frame;
        if (timep > timeq)
            dupb.push_back(pairs[i].second);
        else
            dupa.push_back(pairs[i].first);
    }

    for (size_t i = 0; i < stracksa.size(); i++) {
        auto iter = std::find(dupa.begin(), dupa.end(), static_cast<int>(i));
        if (iter == dupa.end()) {
            resa.push_back(stracksa[i]);
        }
    }

    for (size_t i = 0; i < stracksb.size(); i++) {
        auto iter = std::find(dupb.begin(), dupb.end(), static_cast<int>(i));
        if (iter == dupb.end()) {
            resb.push_back(stracksb[i]);
        }
    }
}

void BYTETracker::linear_assignment(
    std::vector<std::vector<float>>& cost_matrix, int cost_matrix_size,
    int cost_matrix_size_size, float thresh,
    std::vector<std::vector<int>>& matches, std::vector<int>& unmatched_a,
    std::vector<int>& unmatched_b) {
    
    if (cost_matrix.empty()) {
        for (int i = 0; i < cost_matrix_size; i++) {
            unmatched_a.push_back(i);
        }
        for (int i = 0; i < cost_matrix_size_size; i++) {
            unmatched_b.push_back(i);
        }
        return;
    }
    
    std::vector<int> rowsol;
    std::vector<int> colsol;
    lapjv(cost_matrix, rowsol, colsol, true, thresh);
    
    for (size_t i = 0; i < rowsol.size(); i++) {
        if (rowsol[i] >= 0) {
            std::vector<int> match;
            match.push_back(static_cast<int>(i));
            match.push_back(rowsol[i]);
            matches.push_back(match);
        } else {
            unmatched_a.push_back(static_cast<int>(i));
        }
    }
    for (size_t i = 0; i < colsol.size(); i++) {
        if (colsol[i] < 0) {
            unmatched_b.push_back(static_cast<int>(i));
        }
    }
}

void BYTETracker::ious(std::vector<std::vector<float>>& atlbrs,
                       std::vector<std::vector<float>>& btlbrs,
                       std::vector<std::vector<float>>& results) {
    if (atlbrs.size() * btlbrs.size() == 0) return;

    results.resize(atlbrs.size());
    for (size_t i = 0; i < results.size(); i++) {
        results[i].resize(btlbrs.size());
    }

    for (size_t k = 0; k < btlbrs.size(); k++) {
        float box_area = (btlbrs[k][2] - btlbrs[k][0] + 1) * 
                         (btlbrs[k][3] - btlbrs[k][1] + 1);
        for (size_t n = 0; n < atlbrs.size(); n++) {
            float iw = std::min(atlbrs[n][2], btlbrs[k][2]) -
                       std::max(atlbrs[n][0], btlbrs[k][0]) + 1;
            if (iw > 0) {
                float ih = std::min(atlbrs[n][3], btlbrs[k][3]) -
                           std::max(atlbrs[n][1], btlbrs[k][1]) + 1;
                if (ih > 0) {
                    float ua = (atlbrs[n][2] - atlbrs[n][0] + 1) *
                               (atlbrs[n][3] - atlbrs[n][1] + 1) +
                               box_area - iw * ih;
                    results[n][k] = iw * ih / ua;
                } else {
                    results[n][k] = 0.0f;
                }
            } else {
                results[n][k] = 0.0f;
            }
        }
    }
}

void BYTETracker::iou_distance(const STracks& atracks, const STracks& btracks,
                               std::vector<std::vector<float>>& cost_matrix) {
    if (atracks.size() * btracks.size() == 0) return;

    std::vector<std::vector<float>> atlbrs, btlbrs;
    for (size_t i = 0; i < atracks.size(); i++) {
        atlbrs.push_back(atracks[i]->tlbr);
    }
    for (size_t i = 0; i < btracks.size(); i++) {
        btlbrs.push_back(btracks[i]->tlbr);
    }

    std::vector<std::vector<float>> _ious;
    ious(atlbrs, btlbrs, _ious);
    
    for (size_t i = 0; i < _ious.size(); i++) {
        std::vector<float> _iou;
        for (size_t j = 0; j < _ious[i].size(); j++) {
            _iou.push_back(1 - _ious[i][j]);
        }
        cost_matrix.push_back(_iou);
    }
}

void BYTETracker::lapjv(const std::vector<std::vector<float>>& cost,
                        std::vector<int>& rowsol, std::vector<int>& colsol,
                        bool extend_cost, float cost_limit, bool return_cost) {
    std::vector<std::vector<float>> cost_c;
    cost_c.assign(cost.begin(), cost.end());

    int n_rows = static_cast<int>(cost.size());
    int n_cols = static_cast<int>(cost[0].size());
    rowsol.resize(n_rows);
    colsol.resize(n_cols);

    int n = 0;
    if (n_rows == n_cols) {
        n = n_rows;
    } else {
        if (!extend_cost) {
            std::cerr << "set extend_cost=True" << std::endl;
            return;
        }
    }

    if (extend_cost || cost_limit < LONG_MAX) {
        n = n_rows + n_cols;
        std::vector<std::vector<float>> cost_c_extended(n, std::vector<float>(n));

        if (cost_limit < LONG_MAX) {
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    cost_c_extended[i][j] = cost_limit / 2.0f;
                }
            }
        } else {
            float cost_max = -1;
            for (size_t i = 0; i < cost_c.size(); i++) {
                for (size_t j = 0; j < cost_c[i].size(); j++) {
                    if (cost_c[i][j] > cost_max) 
                        cost_max = cost_c[i][j];
                }
            }
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    cost_c_extended[i][j] = cost_max + 1;
                }
            }
        }
        
        for (int i = n_rows; i < n; i++) {
            for (int j = n_cols; j < n; j++) {
                cost_c_extended[i][j] = 0;
            }
        }
        for (int i = 0; i < n_rows; i++) {
            for (int j = 0; j < n_cols; j++) {
                cost_c_extended[i][j] = cost_c[i][j];
            }
        }

        cost_c.clear();
        cost_c.assign(cost_c_extended.begin(), cost_c_extended.end());
    }

    double** cost_ptr = new double*[n];
    for (int i = 0; i < n; i++) {
        cost_ptr[i] = new double[n];
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            cost_ptr[i][j] = cost_c[i][j];
        }
    }

    int* x_c = new int[n];
    int* y_c = new int[n];

    int ret = lapjv_internal(n, cost_ptr, x_c, y_c);
    if (ret != 0) {
        std::cerr << "lapjv_internal failed!" << std::endl;
    }

    if (n != n_rows) {
        for (int i = 0; i < n; i++) {
            if (x_c[i] >= n_cols) x_c[i] = -1;
            if (y_c[i] >= n_rows) y_c[i] = -1;
        }
        for (int i = 0; i < n_rows; i++) {
            rowsol[i] = x_c[i];
        }
        for (int i = 0; i < n_cols; i++) {
            colsol[i] = y_c[i];
        }
    } else {
        for (int i = 0; i < n_rows; i++) {
            rowsol[i] = x_c[i];
        }
        for (int i = 0; i < n_cols; i++) {
            colsol[i] = y_c[i];
        }
    }

    for (int i = 0; i < n; i++) {
        delete[] cost_ptr[i];
    }
    delete[] cost_ptr;
    delete[] x_c;
    delete[] y_c;
}

}  // namespace bytetrack
