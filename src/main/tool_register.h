/*
 * register.h
 *
 *  Created on: 12. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_TOOL_REGISTER_H_
#define SRC_MAIN_TOOL_REGISTER_H_
#include <memory>
#include <typeinfo>

#include <imtjson/value.h>
#include <shared/linear_map.h>

struct ToolDescription {
	std::string id;
	std::string name;
	std::string category;
	json::Value form_def;
};



template<typename T>
class AbstractToolFactory {
public:
	virtual ~AbstractToolFactory() {}
	virtual T create(json::Value config) = 0;
	virtual const ToolDescription &get_description() const = 0;
};

template<typename T>
class AbstractToolRegister {
public:
	virtual ~AbstractToolRegister() {}
	virtual void reg(std::unique_ptr<AbstractToolFactory<T> > &&factory) = 0;

	class RegHlp {
	public:
		AbstractToolRegister &owner;
		ToolDescription &desc;
		RegHlp(AbstractToolRegister &owner, ToolDescription &desc):owner(owner),desc(desc) {}
		RegHlp(const RegHlp &other):owner(owner),desc(desc) {}
		RegHlp &operator=(const RegHlp &other) = delete;
		template<typename Fn>
		void operator>>(Fn &&fn) {

			class Call: public AbstractToolFactory<T> {
			public:
				Call(ToolDescription &desc, Fn &&fn)
					:desc(std::move(desc))
					,fn(std::forward<Fn>(fn)) {}
				virtual T create(json::Value config) override {
					return T(fn(config));
				}
				virtual const ToolDescription &get_description() const {
					return desc;
				}
			protected:
				ToolDescription desc;
				Fn fn;
			};

			owner.reg(std::make_unique<Call>(desc, std::forward<Fn>(fn)));
		}
	};

	RegHlp reg_tool(ToolDescription &&desc) {
		return RegHlp(*this, desc);
	}

};

template<typename T> class ToolName {
public:
	static std::string_view get() {
		return typeid(T).name();
	}
};


template<typename T>
class UnknownToolException: public std::exception {
public:
	UnknownToolException(std::string_view id):id(id) {}
	const virtual char* what() const noexcept override {
		if (msg.empty()) {
			msg.append("Unknown ").append(ToolName<T>::get()).append(": ").append(id);
		}
		return msg.c_str();
	}
protected:
	mutable std::string msg;
	std::string id;
};


template<typename T>
class ToolRegister: public AbstractToolRegister<T> {
public:

	T create(std::string_view id, json::Value config) const {
		auto iter = find(id);
		if (iter == tmap.end()) throw UnknownToolException<T>(id);
		return iter->second->create(config);
	}

	virtual void reg(std::unique_ptr<AbstractToolFactory<T> > &&factory) override {
		const ToolDescription &desc = factory->get_description();
		tmap.emplace(std::string(desc.id), std::move(factory));
	}

	static ToolRegister &getInstance() {
		static ToolRegister inst;
		return inst;
	}

	using ToolMap = ondra_shared::linear_map<std::string, std::unique_ptr<AbstractToolFactory<T> >, std::less<> >;
	auto begin() const {return tmap.begin();}
	auto end() const {return tmap.end();}
	auto find(const std::string_view &id) const {return tmap.find(id);}

protected:
	ToolMap tmap;
};



#endif /* SRC_MAIN_TOOL_REGISTER_H_ */
