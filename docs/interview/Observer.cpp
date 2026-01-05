
/**
 * @file Observer.cpp
 * @brief 观察者模式（Observer Pattern）面试示例 —— “大师兄通知组会/讲论文”
 *
 * 情景：
 * - 老师给大师兄发消息（事件源/被观察者）
 * - 大师兄收到消息后通知所有同学（观察者）
 * - 同学根据消息内容采取不同动作：
 *   - 8:30 组会：自己定 8:00 或更早闹钟
 *   - 13:30 讲论文：做好听论文准备（也可以定提醒）
 *
 * 设计映射：
 * - Subject/Observable（被观察者）：大师兄
 * - Observer（观察者）：师弟/师妹们
 * - Event/Message（事件/消息）：老师的通知（时间 + 主题）
 *
 */

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief 老师消息（事件对象）
 */
struct TeacherMessage {
	std::string time;   ///< 例如 "08:30" / "13:30"
	std::string topic;  ///< 例如 "组会" / "讲论文"
};

/**
 * @brief 观察者接口：同学收到大师兄通知后的统一入口
 */
class IStudentObserver {
public:
	virtual ~IStudentObserver() = default;

	/**
	 * @brief 接收通知
	 * @param msg 老师消息（由大师兄转发）
	 */
	virtual void onNotified(const TeacherMessage& msg) = 0;
};

/**
 * @brief 被观察者（大师兄）
 *
 * 关键点：
 * - 持有观察者列表
 * - 提供订阅/取消订阅
 * - 在事件发生（收到老师消息）时，遍历通知所有观察者
 */
class SeniorBrotherSubject {
public:
	/**
	 * @brief 添加观察者
	 */
	void addObserver(const std::shared_ptr<IStudentObserver>& observer) {
		if (!observer) {
			return;
		}
		observers_.push_back(observer);
	}

	/**
	 * @brief 移除观察者
	 */
	void removeObserver(const std::shared_ptr<IStudentObserver>& observer) {
		observers_.erase(
			std::remove_if(observers_.begin(), observers_.end(),
						   [&](const std::weak_ptr<IStudentObserver>& w) {
							   auto sp = w.lock();
							   return !sp || sp == observer;
						   }),
			observers_.end());
	}

	/**
	 * @brief 大师兄收到老师消息，然后通知所有同学
	 */
	void receiveTeacherMessage(const TeacherMessage& msg) {
		std::cout << "\n【大师兄】收到老师消息：" << msg.time << " - " << msg.topic << "\n";
		notifyAll(msg);
	}

private:
	/**
	 * @brief 通知所有观察者
	 */
	void notifyAll(const TeacherMessage& msg) {
		// 清理已失效的观察者
		observers_.erase(
			std::remove_if(observers_.begin(), observers_.end(),
						   [](const std::weak_ptr<IStudentObserver>& w) {
							   return w.expired();
						   }),
			observers_.end());

		std::cout << "【大师兄】开始通知同学们...（共 " << observers_.size() << " 人）\n";
		for (auto& w : observers_) {
			if (auto sp = w.lock()) {
				sp->onNotified(msg);
			}
		}
	}

private:
	// 用 weak_ptr 的原因：
	// - 避免“被观察者强引用观察者”导致生命周期纠缠
	// - 同学退群/对象销毁后，不会造成悬空指针
	std::vector<std::weak_ptr<IStudentObserver>> observers_;
};

/**
 * @brief 具体观察者：某位同学
 */
class Student final : public IStudentObserver {
public:
	explicit Student(std::string name) : name_(std::move(name)) {}

	void onNotified(const TeacherMessage& msg) override {
		std::cout << "  - 【" << name_ << "】收到通知：" << msg.time << " - " << msg.topic << "\n";

		// 这里就是“收到通知后的业务逻辑”——不同消息做不同响应
		if (msg.topic == "组会") {
			// 8:30 组会：定 8:00 或更早闹钟
			std::cout << "    行动：我定一个 08:00（甚至更早）的闹钟起床！\n";
		} else if (msg.topic == "讲论文") {
			// 13:30 讲论文：准备听论文
			std::cout << "    行动：我把 13:20 设置提醒，提前准备去听论文。\n";
		} else {
			std::cout << "    行动：收到未知主题，先在群里问清楚安排。\n";
		}
	}

private:
	std::string name_;
};

int main() {
	// 1) 大师兄（被观察者）
	SeniorBrotherSubject senior;

	// 2) 同学们（观察者）
	auto studentA = std::make_shared<Student>("二师弟");
	auto studentB = std::make_shared<Student>("三师妹");
	auto studentC = std::make_shared<Student>("小师弟");

	// 3) 订阅（把观察者注册到被观察者）
	senior.addObserver(studentA);
	senior.addObserver(studentB);
	senior.addObserver(studentC);

	// 4) 老师消息1：上午 8:30 来西海岸开组会
	senior.receiveTeacherMessage(TeacherMessage{"08:30", "组会"});

	// 5) 老师消息2：下午 13:30 讲论文
	senior.receiveTeacherMessage(TeacherMessage{"13:30", "讲论文"});

	// 6) 演示：三师妹退群（取消订阅）
	std::cout << "\n【系统】三师妹今天请假，不再接收通知（removeObserver）\n";
	senior.removeObserver(studentB);

	// 7) 再发一次消息，验证只有剩余观察者会收到
	senior.receiveTeacherMessage(TeacherMessage{"08:30", "组会"});

	return 0;
}

