#include "mesher/repair_zipper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Tri=std::array<VertexId,3>;
using Edge=std::pair<VertexId,VertexId>;
Edge edge(VertexId a,VertexId b) {if(a>b)std::swap(a,b);return {a,b};}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(b-a,b-a)+dot(c-b,c-b)+dot(a-c,a-c);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
struct FaceRec {FaceId original{}; PartitionId owner{};Tri vertices{};bool seam{};double target{};};
double median(const RegionTopologyIndex& mesh,const RepairContour& loop) {
    std::vector<double> spans;
    for(std::size_t i=0;i<loop.vertexIds.size();++i) {
        const auto a=mesh.vertex(loop.vertexIds[i]).position;
        const auto b=mesh.vertex(loop.vertexIds[(i+1)%loop.vertexIds.size()]).position;
        spans.push_back(length(a-b));
    }
    std::sort(spans.begin(),spans.end());
    return spans[spans.size()/2];
}
struct Match {
    std::vector<std::pair<std::size_t,std::size_t>> pairs;
    double score{std::numeric_limits<double>::infinity()};
};
Match pairLoops(const RegionTopologyIndex& mesh,
                const std::vector<RepairContour>& loops,
                const ForcedZipperSettings& bounds) {
    const auto n=loops.size();
    if(!n||n%2||n>bounds.maxLoops)
        throw std::logic_error("R04 requires a positive even bounded number of zipper loops; no implicit cap");
    std::vector<double> h(n);
    for(std::size_t i=0;i<n;++i) {
        if(loops[i].vertexIds.size()<3||
           loops[i].vertexIds.size()>bounds.maxContourVertices)
            throw std::length_error("R04 contour vertex limit exceeded");
        h[i]=median(mesh,loops[i]);
        if(!(h[i]>0)||!std::isfinite(h[i]))
            throw std::logic_error("R04 degenerate contour edge scale");
    }
    std::vector<std::vector<double>> scores(
        n,std::vector<double>(n,std::numeric_limits<double>::infinity()));
    for(std::size_t a=0;a<n;++a)for(std::size_t b=a+1;b<n;++b) {
        const auto ratio=std::max(loops[a].perimeter,loops[b].perimeter)/
                         std::min(loops[a].perimeter,loops[b].perimeter);
        if(!std::isfinite(ratio)||ratio>bounds.maximumPerimeterRatio)continue;
        double sum=0;
        double maximum=0;
        for(const auto& [i,j]:{std::pair{a,b},std::pair{b,a}}) {
            for(const auto vertex:loops[i].vertexIds) {
                const auto p=mesh.vertex(vertex).position;
                double nearest=std::numeric_limits<double>::infinity();
                for(const auto other:loops[j].vertexIds)
                    nearest=std::min(nearest,
                        length(p-mesh.vertex(other).position));
                maximum=std::max(maximum,nearest);
                sum+=nearest/h[i]/loops[i].vertexIds.size();
            }
        }
        if(maximum>bounds.maxPairDistanceInEdges*
                      std::max(h[a],h[b]))continue;
        scores[a][b]=scores[b][a]=sum;
    }
    std::vector<Match> results;
    std::vector<std::pair<std::size_t,std::size_t>> working;
    const auto search=[&](auto&& self,std::size_t used,double cost)->void {
        if(used==(std::size_t{1}<<n)-1) {
            results.push_back({working,cost});return;
        }
        std::size_t first=0;
        while(used&(std::size_t{1}<<first))++first;
        for(std::size_t j=first+1;j<n;++j) {
            if(used&(std::size_t{1}<<j)||!std::isfinite(scores[first][j]))continue;
            working.emplace_back(first,j);
            self(self,used|(std::size_t{1}<<first)|(std::size_t{1}<<j),
                 cost+scores[first][j]);
            working.pop_back();
        }
    };
    search(search,0,0);
    if(results.empty())throw std::logic_error(
        "R04 no complete geometrically supported pairing; no guessed cap or zipper");
    std::sort(results.begin(),results.end(),[](const Match& a,const Match& b) {
        return a.score<b.score;
    });
    if(results.size()>1&&results[1].score<=
       results[0].score*(1+bounds.minMatchingAdvantage))
        throw std::logic_error("R04 many-to-many/near-tied loop matching is ambiguous");
    return results.front();
}
struct PathStep{double cost=std::numeric_limits<double>::infinity();int previous{};};
std::vector<Tri> zipperTriangles(
    const std::vector<VertexId>& first,const std::vector<VertexId>& second,
    const std::map<VertexId,Vec3>& pos,
    const ForcedZipperSettings& bounds) {
    std::vector<VertexId> x=first,y(second.rbegin(),second.rend());
    const std::size_t n=x.size(),m=y.size();
    std::size_t offset=0;
    double distance=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<m;++i) {
        const auto delta=pos.at(x.front())-pos.at(y[i]);
        const auto d=dot(delta,delta);
        if(d<distance){distance=d;offset=i;}
    }
    std::rotate(y.begin(),y.begin()+static_cast<std::ptrdiff_t>(offset),y.end());
    x.push_back(x.front());y.push_back(y.front());
    const auto phase=[&](const std::vector<VertexId>& ring) {
        std::vector<double> cumulative(ring.size());
        for(std::size_t i=1;i<ring.size();++i)
            cumulative[i]=cumulative[i-1]+length(
                pos.at(ring[i])-pos.at(ring[i-1]));
        if(!(cumulative.back()>0))throw std::logic_error("R04 zero contour perimeter");
        for(auto& v:cumulative)v/=cumulative.back();
        return cumulative;
    };
    const auto ax=phase(x),by=phase(y);
    const auto stride=m+1;
    std::vector<PathStep> dp((n+1)*(m+1));
    dp[0].cost=0;
    for(std::size_t i=0;i<=n;++i)for(std::size_t j=0;j<=m;++j) {
        if(i==0&&j==0)continue;
        if(std::abs(ax[i]-by[j])>.34)continue;
        auto& at=dp[i*stride+j];
        const auto cost=[&](Tri tri) {
            const auto a=pos.at(tri[0]),b=pos.at(tri[1]),c=pos.at(tri[2]);
            const auto q=quality(a,b,c);
            if(q<.00001)return std::numeric_limits<double>::infinity();
            return (length(a-b)+length(b-c)+length(c-a))+
                .04*std::max({length(a-b),length(b-c),length(c-a)})/q;
        };
        if(i>0) {
            const auto candidate=dp[(i-1)*stride+j].cost+
                                  cost({x[i],x[i-1],y[j]});
            if(candidate<at.cost){at.cost=candidate;at.previous=1;}
        }
        if(j>0) {
            const auto candidate=dp[i*stride+j-1].cost+
                                  cost({x[i],y[j-1],y[j]});
            if(candidate<at.cost){at.cost=candidate;at.previous=2;}
        }
    }
    if(!std::isfinite(dp.back().cost))
        throw std::logic_error("R04 no nondegenerate zipper triangulation");
    std::size_t i=n,j=m;
    std::vector<Tri> triangles;
    while(i||j) {
        const auto type=dp[i*stride+j].previous;
        if(type==1) {triangles.push_back({x[i],x[i-1],y[j]});--i;}
        else if(type==2){triangles.push_back({x[i],y[j-1],y[j]});--j;}
        else throw std::logic_error("R04 zipper strip missing a DP predecessor");
    }
    std::reverse(triangles.begin(),triangles.end());
    if(triangles.size()!=n+m)throw std::logic_error("R04 strip has wrong topology");
    (void)bounds;
    return triangles;
}
void splitTracked(std::vector<FaceRec>& faces,VertexId a,VertexId b,
                  VertexId mid,std::size_t expected,
                  const ForcedZipperSettings& bounds) {
    std::size_t found=0;
    const auto original=faces.size();
    for(std::size_t index=0;index<original;++index) {
        const auto v=faces[index].vertices;
        for(unsigned j=0;j<3;++j) {
            if(edge(v[j],v[(j+1)%3])!=edge(a,b))continue;
            const auto c=v[(j+2)%3];
            faces[index].vertices={v[j],mid,c};
            faces.push_back({0,faces[index].owner,
                             {mid,v[(j+1)%3],c},
                             faces[index].seam,faces[index].target});
            ++found;
            break;
        }
    }
    if(found!=expected)
        throw std::logic_error("R04 split edge has an unexpected number of tracked incident faces");
    if(faces.size()>bounds.maxNewFaces+1024)
        throw std::length_error("R04 split face work limit exceeded");
}
void auditClosed(const RegionTopologyIndex& mesh,std::size_t maxFaces) {
    struct Ref{std::size_t count{};bool positive{};};
    std::map<Edge,Ref> edges;
    std::size_t faces=0;
    std::set<VertexId> present;
    mesh.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& v:part.vertices)present.insert(v.id);
        faces+=part.faces.size();
        if(faces>maxFaces)throw std::length_error("R04 closed audit face limit exceeded");
        for(const auto& f:part.faces) {
            for(unsigned i=0;i<3;++i) {
                const auto a=f.vertices[i],b=f.vertices[(i+1)%3];
                const auto key=edge(a,b);
                auto& current=edges[key];
                if(current.count&&(current.count>1||current.positive==(key.first==a)))
                    throw std::logic_error("R04 seam is nonmanifold or has inconsistent face winding");
                if(!current.count)current.positive=(key.first==a);
                ++current.count;
            }
        }
    });
    for(const auto& [_,data]:edges)
        if(data.count!=2)
            throw std::logic_error("R04 forced zipper did not close every hole");
    // Every open mesh vertex must be in at least one remaining face;
    // the R03 stage has already eliminated all orphans before zippering.
    for(const auto id:present) {
        const auto v=mesh.vertex(id);
        if(v.incidentFaces.empty())
            throw std::logic_error("R04 result contains an isolated vertex");
        std::map<VertexId,std::set<VertexId>> link;
        for(const auto faceId:v.incidentFaces) {
            const auto& f=mesh.face(faceId).vertices;
            std::array<VertexId,2> rest{};int count=0;
            for(const auto corner:f)if(corner!=id)rest[count++]=corner;
            if(count!=2)throw std::logic_error("R04 malformed vertex fan");
            link[rest[0]].insert(rest[1]);link[rest[1]].insert(rest[0]);
        }
        for(const auto& [_,n]:link)
            if(n.size()!=2)throw std::logic_error("R04 branching zipper vertex fan");
        std::set<VertexId> visited;
        std::vector<VertexId> stack{link.begin()->first};
        while(!stack.empty()) {
            const auto x=stack.back();stack.pop_back();
            if(!visited.insert(x).second)continue;
            for(const auto q:link.at(x))stack.push_back(q);
        }
        if(visited.size()!=link.size())
            throw std::logic_error("R04 seam has disconnected vertex fans");
    }
}
} // namespace

ForcedZipperResult applyForcedPrivateZipper(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const ForcedZipperSettings& limits) {
    if(!cut.index||!proposal.complete||!limits.maxNewVertices||
       !limits.maxNewFaces||!limits.maxRefinementSteps||!limits.maxAuditedFaces||
       !std::isfinite(limits.maximumPerimeterRatio)||
       !std::isfinite(limits.maxPairDistanceInEdges)||
       !std::isfinite(limits.minMatchingAdvantage)||
       !std::isfinite(limits.maxBoundaryEdgeFactor)||
       !std::isfinite(limits.maxSeamEdgeFactor)||
       !std::isfinite(limits.minSeamQuality)||
       limits.maxBoundaryEdgeFactor<=0||limits.maxSeamEdgeFactor<=1||
       limits.minSeamQuality<=0||limits.minSeamQuality>=1)
        throw std::invalid_argument("R04 requires complete cut and valid finite budgets");
    if(cut.boundaryLoops!=proposal.contours.size()||
       cut.boundaryEdges!=proposal.boundaryEdges)
        throw std::logic_error("R04 refuses a stale cut/loop count mismatch");
    auto pairing=pairLoops(*cut.index,proposal.contours,limits);
    auto view=cut.index->revision().createMutableView();
    ForcedZipperResult output;
    output.matchedLoops=pairing.pairs;
    std::map<VertexId,Vec3> pos;
    std::map<VertexId,PartitionId> vertexOwners;
    std::set<PartitionId> owners;
    std::vector<FaceRec> faces;
    std::map<FaceId,std::size_t> existing;
    std::vector<std::vector<VertexId>> loops;
    loops.reserve(proposal.contours.size());
    for(const auto& loop:proposal.contours) {
        loops.push_back(loop.vertexIds);
        for(const auto id:loop.vertexIds) {
            pos.emplace(id,cut.index->vertex(id).position);
            vertexOwners.emplace(id,cut.index->vertex(id).owner);
        }
        for(const auto id:loop.survivingFaces) {
            if(existing.contains(id))continue;
            const auto face=cut.index->face(id);
            existing.emplace(id,faces.size());
            faces.push_back({id,face.owner,face.vertices,false,0});
            for(const auto vertex:face.vertices) {
                pos.emplace(vertex,cut.index->vertex(vertex).position);
                vertexOwners.emplace(vertex,cut.index->vertex(vertex).owner);
            }
        }
    }
    const auto makeVertex=[&](PartitionId owner,Vec3 point)->VertexId {
        if(output.insertedVertices.size()>=limits.maxNewVertices)
            throw std::length_error("R04 finite inserted-vertex budget exceeded");
        owners.insert(owner);
        const auto id=view.addVertex(owner,point);
        pos.emplace(id,point);
        vertexOwners.emplace(id,owner);
        output.insertedVertices.push_back(id);
        return id;
    };
    const auto span=[&](VertexId a,VertexId b) {
        return length(pos.at(a)-pos.at(b));
    };
    for(const auto& [i,j]:pairing.pairs) {
        const auto h=std::min(median(*cut.index,proposal.contours[i]),
                              median(*cut.index,proposal.contours[j]));
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R04 invalid seam density");
        for(const auto k:{i,j}) {
            const auto original=loops[k];
            std::vector<VertexId> refined;
            for(std::size_t e=0;e<original.size();++e) {
                const auto a=original[e],b=original[(e+1)%original.size()];
                refined.push_back(a);
                const auto segments=static_cast<std::size_t>(std::ceil(
                    span(a,b)/(limits.maxBoundaryEdgeFactor*h)));
                if(segments>16)
                    throw std::length_error("R04 boundary edge needs more than 16 subdivisions");
                auto last=a;
                const auto owner=cut.index->vertex(a).owner;
                for(std::size_t part=1;part<segments;++part) {
                    const auto middle=makeVertex(owner,
                        pos.at(a)+(pos.at(b)-pos.at(a))*
                        (static_cast<double>(part)/segments));
                    splitTracked(faces,last,b,middle,1,limits);
                    refined.push_back(middle);
                    last=middle;
                    ++output.splitBoundaryEdges;
                }
            }
            if(refined.size()>limits.maxContourVertices)
                throw std::length_error("R04 refined loop exceeds vertex budget");
            loops[k]=std::move(refined);
        }
        const auto owner=cut.index->face(
            proposal.contours[i].survivingFaces.front()).owner;
        for(const auto& triangle:zipperTriangles(loops[i],loops[j],pos,limits))
            faces.push_back({0,owner,triangle,true,h});
    }
    // Split the geometric longest offending seam edge first. For skinny
    // faces, evaluate all three possible bisections, including both sides
    // of the edge, rather than giving every edge the same quality penalty.
    // The old equal-score choice repeatedly cut the first edge of an
    // ill-shaped triangle, consuming the budget without improving its angle.
    const auto midpoint=[&](Edge e) {
        return (pos.at(e.first)+pos.at(e.second))*.5;
    };
    const auto splitQuality=[&](const FaceRec& face,Edge chosen) {
        const auto v=face.vertices;
        for(unsigned i=0;i<3;++i) {
            const auto a=v[i],b=v[(i+1)%3],c=v[(i+2)%3];
            if(edge(a,b)!=chosen)continue;
            const auto p=midpoint(chosen);
            return std::min(quality(pos.at(a),p,pos.at(c)),
                            quality(p,pos.at(b),pos.at(c)));
        }
        throw std::logic_error("R04 predicted bisection has no owning triangle");
    };
    const auto incidentFaces=[&](Edge e) {
        std::vector<std::size_t> ids;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto v=faces[i].vertices;
            for(unsigned j=0;j<3;++j)
                if(edge(v[j],v[(j+1)%3])==e) {
                    ids.push_back(i);break;
                }
        }
        return ids;
    };
    std::size_t stagnation=0;
    for(std::size_t step=0;step<limits.maxRefinementSteps;++step) {
        Edge longest{};
        double largestRatio=1;
        std::size_t lowestIndex=faces.size();
        double lowestQuality=limits.minSeamQuality;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& face=faces[i];
            if(!face.seam)continue;
            const auto& v=face.vertices;
            const auto q=quality(pos.at(v[0]),pos.at(v[1]),pos.at(v[2]));
            if(q<lowestQuality) {lowestIndex=i;lowestQuality=q;}
            for(unsigned k=0;k<3;++k) {
                const auto candidate=edge(v[k],v[(k+1)%3]);
                const auto ratio=span(candidate.first,candidate.second)/
                    (limits.maxSeamEdgeFactor*face.target);
                if(ratio>largestRatio) {
                    largestRatio=ratio;
                    longest=candidate;
                }
            }
        }
        if(largestRatio<=1&&lowestIndex==faces.size())break;
        Edge worst=longest;
        if(largestRatio<=1) {
            const auto v=faces[lowestIndex].vertices;
            double bestScore=-1;
            for(unsigned k=0;k<3;++k) {
                const auto candidate=edge(v[k],v[(k+1)%3]);
                const auto adjacent=incidentFaces(candidate);
                if(adjacent.size()!=2)continue;
                double score=1;
                for(const auto index:adjacent)
                    score=std::min(score,splitQuality(faces[index],candidate));
                // Prefer an edge with the highest predicted minimum child
                // quality; tie-break toward the longer physical edge.
                score+=1e-7*span(candidate.first,candidate.second)/
                    faces[lowestIndex].target;
                if(score>bestScore){bestScore=score;worst=candidate;}
            }
            if(bestScore<0)
                throw std::logic_error("R04 skinny seam has no two-sided refinement edge");
            if(bestScore<=lowestQuality*(1+1e-6))++stagnation;
            else stagnation=0;
        }else stagnation=0;
        if(stagnation>64)
            throw std::logic_error(
                "R04 seam remains skinny: edge bisection cannot improve local angles");
        const auto adjacent=incidentFaces(worst);
        if(adjacent.size()!=2)
            throw std::logic_error("R04 new seam edge not bounded by exactly two affected faces");
        const auto owner=vertexOwners.at(worst.first);
        const auto newVertex=makeVertex(owner,midpoint(worst));
        splitTracked(faces,worst.first,worst.second,newVertex,2,limits);
        ++output.refinementSteps;
    }
    for(const auto& face:faces)if(face.seam) {
        const auto v=face.vertices;
        const auto q=quality(pos.at(v[0]),pos.at(v[1]),pos.at(v[2]));
        const auto maximum=std::max({span(v[0],v[1]),span(v[1],v[2]),span(v[2],v[0])});
        if(q<limits.minSeamQuality||
           maximum>limits.maxSeamEdgeFactor*face.target*(1+1e-8))
            throw std::logic_error(
                "R04 zipper scar still has elongated/skinny triangles after finite subdivision:"
                " min_q="+std::to_string(q)+
                " required_q="+std::to_string(limits.minSeamQuality)+
                " max_edge_ratio="+std::to_string(maximum/(limits.maxSeamEdgeFactor*face.target))+
                " splits="+std::to_string(output.refinementSteps)+
                " contour_edge_splits="+std::to_string(output.splitBoundaryEdges));
        output.minimumSeamQuality=std::min(output.minimumSeamQuality,q);
    }
    for(const auto& face:faces)if(!face.seam) {
        const auto v=face.vertices;
        if(quality(pos.at(v[0]),pos.at(v[1]),pos.at(v[2]))<
           limits.minSurvivorQuality)
            throw std::logic_error("R04 adjacent surviving triangle became too skinny");
    }
    for(const auto& face:faces) {
        owners.insert(face.owner);
        if(face.original) {
            auto& partition=view.editPartition(face.owner);
            const auto it=std::find_if(partition.faces.begin(),partition.faces.end(),
                [&](const PartitionFace& current){return current.id==face.original;});
            if(it==partition.faces.end())
                throw std::logic_error("R04 edited retained FaceId was lost");
            it->vertices=face.vertices;
        }else {
            if(output.totalNewFaces>=limits.maxNewFaces)
                throw std::length_error("R04 finite new-face budget exhausted");
            const auto id=view.addFace(face.owner,face.vertices);
            ++output.totalNewFaces;
            if(face.seam)output.blueFaces.push_back(id);
        }
    }
    output.touchedPartitions.assign(owners.begin(),owners.end());
    const auto revision=view.commit().revision;
    auto result=RegionTopologyIndex::restore(revision,
        cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    auditClosed(*result,limits.maxAuditedFaces);
    output.closedEdges=cut.boundaryEdges;
    output.index=std::move(result);
    return output;
}
void validatePrivateClosedManifold(const RegionTopologyIndex& mesh,std::size_t maxFaces) {
    auditClosed(mesh,maxFaces);
}
} // namespace mesher
