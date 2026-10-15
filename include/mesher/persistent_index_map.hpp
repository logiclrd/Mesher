#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mesher {

// Persistent AVL map. A write clones O(log N) nodes on one root-to-leaf
// path, retaining unchanged nodes AND value object identities. No parent
// pointers, whole-map copies, global caches, or revision ancestry walks.
// The owner must finish mutations before publishing this map to readers.
template<class Key,class Value,class Compare=std::less<Key>>
class PersistentIndexMap {
    struct Node;
    using NodePtr=std::shared_ptr<const Node>;
    struct Node {
        Key key;
        std::shared_ptr<const Value> value;
        NodePtr left,right;
        int height;
        Node(Key k,std::shared_ptr<const Value> v,NodePtr l,NodePtr r)
            :key(std::move(k)),value(std::move(v)),
             left(std::move(l)),right(std::move(r)),
             height(1+std::max(heightOf(left),heightOf(right))) {}
    };
    NodePtr root_;
    std::size_t count_{};
    Compare compare_{};
    static int heightOf(const NodePtr& n) {return n ? n->height:0;}
    static NodePtr node(Key key,std::shared_ptr<const Value> value,
                        NodePtr left,NodePtr right,std::size_t& copied) {
        ++copied;
        return std::make_shared<Node>(std::move(key),std::move(value),
                                      std::move(left),std::move(right));
    }
    static NodePtr rotateRight(const NodePtr& n,std::size_t& copied) {
        const auto& left=n->left;
        auto lower=node(n->key,n->value,left->right,n->right,copied);
        return node(left->key,left->value,left->left,std::move(lower),copied);
    }
    static NodePtr rotateLeft(const NodePtr& n,std::size_t& copied) {
        const auto& right=n->right;
        auto lower=node(n->key,n->value,n->left,right->left,copied);
        return node(right->key,right->value,std::move(lower),right->right,copied);
    }
    static NodePtr balance(NodePtr n,std::size_t& copied) {
        const int diff=heightOf(n->left)-heightOf(n->right);
        if(diff>1) {
            if(heightOf(n->left->right)>heightOf(n->left->left)) {
                auto corrected=rotateLeft(n->left,copied);
                n=node(n->key,n->value,std::move(corrected),n->right,copied);
            }
            return rotateRight(n,copied);
        }
        if(diff< -1) {
            if(heightOf(n->right->left)>heightOf(n->right->right)) {
                auto corrected=rotateRight(n->right,copied);
                n=node(n->key,n->value,n->left,std::move(corrected),copied);
            }
            return rotateLeft(n,copied);
        }
        return n;
    }
    NodePtr put(NodePtr n,const Key& key,std::shared_ptr<const Value> value,
                bool& inserted,std::size_t& copied) const {
        if(!n) {
            inserted=true;
            return node(key,std::move(value),nullptr,nullptr,copied);
        }
        if(compare_(key,n->key))
            return balance(node(n->key,n->value,
                                put(n->left,key,std::move(value),inserted,copied),
                                n->right,copied),copied);
        if(compare_(n->key,key))
            return balance(node(n->key,n->value,n->left,
                                put(n->right,key,std::move(value),inserted,copied),
                                copied),copied);
        return node(n->key,std::move(value),n->left,n->right,copied);
    }
    static NodePtr minimum(NodePtr n) {
        while(n->left)n=n->left;
        return n;
    }
    NodePtr eraseNode(NodePtr n,const Key& key,bool& removed,
                      std::size_t& copied) const {
        if(!n)return nullptr;
        if(compare_(key,n->key)) {
            auto changed=eraseNode(n->left,key,removed,copied);
            if(!removed)return n;
            return balance(node(n->key,n->value,std::move(changed),
                                n->right,copied),copied);
        }
        if(compare_(n->key,key)) {
            auto changed=eraseNode(n->right,key,removed,copied);
            if(!removed)return n;
            return balance(node(n->key,n->value,n->left,
                                std::move(changed),copied),copied);
        }
        removed=true;
        if(!n->left)return n->right;
        if(!n->right)return n->left;
        const auto next=minimum(n->right);
        bool erased=false;
        auto right=eraseNode(n->right,next->key,erased,copied);
        return balance(node(next->key,next->value,n->left,
                            std::move(right),copied),copied);
    }
    template<class Fn>
    static void walk(const NodePtr& n,const Fn& fn) {
        if(!n)return;
        walk(n->left,fn);
        fn(n->key,*n->value);
        walk(n->right,fn);
    }
public:
    std::size_t size() const noexcept {return count_;}
    std::shared_ptr<const Value> find(const Key& key) const {
        auto n=root_;
        while(n) {
            if(compare_(key,n->key))n=n->left;
            else if(compare_(n->key,key))n=n->right;
            else return n->value;
        }
        return nullptr;
    }
    const Value& at(const Key& key) const {
        auto result=find(key);
        if(!result)throw std::out_of_range("Missing persistent index entry");
        return *result;
    }
    void set(const Key& key,Value value,std::size_t* copiedNodes=nullptr) {
        std::size_t count=0;
        bool inserted=false;
        auto next=put(root_,key,std::make_shared<const Value>(std::move(value)),
                      inserted,count);
        root_=std::move(next);
        if(inserted)++count_;
        if(copiedNodes)*copiedNodes+=count;
    }
    void erase(const Key& key,std::size_t* copiedNodes=nullptr) {
        std::size_t count=0;
        bool removed=false;
        auto next=eraseNode(root_,key,removed,count);
        if(removed) {
            root_=std::move(next);
            --count_;
        }
        if(copiedNodes)*copiedNodes+=count;
    }
    template<class Fn>
    void forEach(const Fn& fn) const {walk(root_,fn);}
};

} // namespace mesher
